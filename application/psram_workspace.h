#ifndef EE02_PSRAM_WORKSPACE_H
#define EE02_PSRAM_WORKSPACE_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <Seeed_GFX.h>
#include <esp32-hal-psram.h>
#include <panel/Panel_EPaper.h>
#include <runtime/DisplayInstance.h>
#include <runtime/ProductCatalog.h>

// One application allocation; not yet used by the production sketch.
// Single-threaded callbacks must return normally, finish all users and discard
// aliases before returning. C++ cannot revoke escaped pointers or stop races.
class Ee02PsramWorkspace {
public:
    enum class State { Unallocated, Scratch, ScratchAccess, Rendering, Retained };
    static constexpr size_t FrameBufferBytes = 1200U * 1600U / 2U;
    using ScratchCallback = GfxResult (*)(uint8_t*, size_t, void*);
    using DrawCallback = GfxResult (*)(Seeed_GFX&, void*);

    struct RenderResult {
        GfxResult operation;
        GfxResult shutdown;
        bool shutdownChecked;
        bool ok() const {
            return operation.ok() && shutdownChecked && shutdown.ok();
        }
    };

    Ee02PsramWorkspace() = default;
    Ee02PsramWorkspace(const Ee02PsramWorkspace&) = delete;
    Ee02PsramWorkspace& operator=(const Ee02PsramWorkspace&) = delete;
    Ee02PsramWorkspace(Ee02PsramWorkspace&&) = delete;
    Ee02PsramWorkspace& operator=(Ee02PsramWorkspace&&) = delete;

    ~Ee02PsramWorkspace() {
        // Retained/busy storage is intentionally not freed. Destruction proves
        // neither physical quiescence nor that externally escaped aliases died.
        if (_state == State::Scratch) free(_buffer);
    }

    State state() const { return _state; }
    size_t capacity() const { return _capacity; }

    GfxResult allocate(size_t capacity) {
        if (_state != State::Unallocated) return unavailable();
        // PTRDIFF_MAX is an arithmetic/object-span bound, not a product budget.
        if (capacity < FrameBufferBytes || capacity > static_cast<size_t>(PTRDIFF_MAX)) {
            return GfxResult(GfxError::InvalidArgument, "invalid workspace capacity");
        }
        if (!psramFound()) {
            return GfxResult(GfxError::AllocationFailed, "workspace PSRAM unavailable");
        }
        // Core 3.3.12: SPIRAM | 8BIT only, with no internal-RAM fallback.
        uint8_t* buffer = static_cast<uint8_t*>(ps_malloc(capacity));
        if (!buffer) {
            return GfxResult(GfxError::AllocationFailed, "workspace PSRAM allocation failed");
        }
        if (capacity > UINTPTR_MAX - reinterpret_cast<uintptr_t>(buffer)) {
            free(buffer);
            return GfxResult(GfxError::InvalidArgument, "workspace address span overflows");
        }
        _buffer = buffer;
        _capacity = capacity;
        _state = State::Scratch;
        return GfxResult::success();
    }

    // Direct access avoids compulsory parser copies. Only the requested byte
    // range is authorized, for this callback's duration. Never free/reallocate
    // the borrowed pointer or destroy this owner in a callback. Typed alignment/object
    // construction/destruction are caller responsibilities; no parser allocator.
    GfxResult withScratch(size_t offset, size_t bytes, ScratchCallback use,
                           void* context = nullptr) {
        if (_state != State::Scratch) return unavailable();
        if (!use || offset > _capacity || bytes > _capacity - offset) {
            return GfxResult(GfxError::InvalidArgument, "invalid scratch range or callback");
        }
        _state = State::ScratchAccess;
        ScratchScope scope(_state);
        return use(_buffer + offset, bytes, context);
    }

    // Drawing only: do not retain gfx/panel/framebuffer references or change
    // their ownership/lifecycle. Network/parser work must already be finished.
    // This operation owns factory -> borrow -> begin/configure -> refresh/end
    // cleanup; callbacks may draw and change coordinate rotation.
    RenderResult render(DrawCallback draw, void* context = nullptr) {
        RenderResult result = {GfxResult::success(),
            GfxResult(GfxError::NotInitialized, "panel shutdown not attempted"), false};
        if (_state != State::Scratch) {
            result.operation = unavailable();
            return result;
        }
        if (!draw) {
            result.operation = GfxResult(GfxError::InvalidArgument, "null drawing callback");
            return result;
        }
        _state = State::Rendering;
        DisplayInstance instance;
        result.operation = ProductCatalog::create(
            Seeed_Product::Seeed_ePaper_13INCH3_C, instance);
        if (!result.operation.ok()) {
            instance.reset();
            _state = State::Scratch;
            return result;
        }
        // The fixed official product constructs Panel_EPaper; no RTTI needed.
        auto* panel = static_cast<Panel_EPaper*>(instance.panel());
        result.operation = panel->setBorrowedFrameBuffer(_buffer, FrameBufferBytes);
        if (!result.operation.ok()) {
            instance.reset();
            _state = State::Scratch;
            return result;
        }
        {
            // The instance outlives this externally attached, non-owning facade.
            Seeed_GFX display(*panel);
            if (!display.begin()) {
                result.operation = panel->lastResult();
                if (result.operation.ok()) {
                    result.operation = GfxResult(GfxError::PanelInitFailed,
                                                  "workspace panel begin failed");
                }
            } else {
                result.operation = panel->configure(PanelMode::Colorful);
                if (result.operation.ok()) result.operation = draw(display, context);
                if (result.operation.ok()) result.operation = panel->refresh();
            }
            // Preserve operation independently: successful end must not hide a
            // refresh/draw failure. Facade/instance repeated end can mask errors.
            result.shutdown = panel->end();
            result.shutdownChecked = true;
        }
        instance.reset(); // Remove software references while storage stays alive.
        if (uncertain(result.operation) || !result.shutdown.ok()) {
            _restriction = uncertain(result.operation) ? result.operation : result.shutdown;
            _state = State::Retained;
        } else {
            _state = State::Scratch;
        }
        return result;
    }

    GfxResult release() {
        if (_state == State::Unallocated) return GfxResult::success();
        if (_state != State::Scratch) return unavailable();
        free(_buffer);
        _buffer = nullptr;
        _capacity = 0;
        _state = State::Unallocated;
        return GfxResult::success();
    }

private:
    uint8_t* _buffer = nullptr;
    size_t _capacity = 0;
    State _state = State::Unallocated;
    GfxResult _restriction;

    struct ScratchScope {
        State& state;
        explicit ScratchScope(State& s) : state(s) {}
        ~ScratchScope() { state = State::Scratch; }
    };

    static bool uncertain(GfxResult result) {
        return result.error == GfxError::BusyTimeout ||
               result.error == GfxError::CommunicationFailed;
    }

    GfxResult unavailable() const {
        if (_state == State::Retained) return _restriction;
        return GfxResult(_state == State::Unallocated ? GfxError::NotInitialized
                                                       : GfxError::InvalidArgument,
                         "workspace operation unavailable in this phase");
    }
};

#endif
