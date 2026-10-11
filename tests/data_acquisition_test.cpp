#include "../data_acquisition.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
struct FakeSession {
  std::array<bool, 4> outcomes;
  std::array<int, 4> calls;
  std::vector<int> order;
  int reports;
  int disconnects;
  DataAcquisitionResults reported;
};

FakeSession session;

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool fetch(int source) {
  ++session.calls[source];
  session.order.push_back(source);
  return session.outcomes[source];
}

bool weather() { return fetch(0); }
bool forecast() { return fetch(1); }
bool dailyForecast() { return fetch(2); }
bool calendar() { return fetch(3); }

void report(const DataAcquisitionResults &results) {
  ++session.reports;
  session.order.push_back(4);
  session.reported = results;
}

void disconnect() {
  ++session.disconnects;
  session.order.push_back(5);
}

void checkConnected(unsigned int successMask) {
  session = {};
  for (unsigned int i = 0; i < 4; ++i) {
    session.outcomes[i] = (successMask & (1U << i)) != 0;
  }

  const bool result = acquireDataSources(true, weather, forecast, dailyForecast,
                                         calendar, report, disconnect);

  require(session.calls == std::array<int, 4>{{1, 1, 1, 1}},
          "each source must be attempted exactly once despite other failures");
  require(session.order == std::vector<int>({0, 1, 2, 3, 4, 5}),
          "requests must retain order, then report and disconnect");
  require(session.reports == 1 && session.disconnects == 1,
          "connected acquisition must report and disconnect once");
  const std::array<bool, 4> reported = {{
      session.reported.weather, session.reported.forecast,
      session.reported.dailyForecast, session.reported.calendar}};
  require(reported == session.outcomes,
          "individual outcomes must match the actual fetch results");
  require(result == session.outcomes[0],
          "return value must depend only on current-weather success");
}

void checkDisconnected() {
  session = {};
  session.outcomes = {{true, true, true, true}};

  const bool result = acquireDataSources(false, weather, forecast, dailyForecast,
                                         calendar, report, disconnect);

  require(!result, "Wi-Fi failure must return false");
  require(session.calls == std::array<int, 4>{{0, 0, 0, 0}},
          "Wi-Fi failure must not attempt any requests");
  require(session.order.empty() && session.reports == 0 &&
              session.disconnects == 0,
          "Wi-Fi failure must preserve the early exit without cleanup callbacks");
}
} // namespace

int main() {
  // Exhaust all result combinations: all success, each single failure, every
  // multiple-failure combination, and all failure.
  for (unsigned int mask = 0; mask < 16; ++mask) {
    checkConnected(mask);
  }
  checkDisconnected();
  std::cout << "PASS: 16 request-result combinations and Wi-Fi failure\n";
}
