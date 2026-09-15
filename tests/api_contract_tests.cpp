#include "office3ds/api.hpp"

#include <cassert>
#include <type_traits>

int main() {
  static_assert(office3ds::api::OFFICE_3DS_PRODUCT_API_VERSION == 1);
  static_assert(std::has_virtual_destructor_v<office3ds::api::Adapter>);
  static_assert(std::has_virtual_destructor_v<office3ds::api::DashboardLoad>);

  office3ds::api::DashboardSnapshot snapshot;
  snapshot.profile.display_name = "Demo Person";
  snapshot.worklog.push_back({"2026-01-05", 420, 480});
  assert(snapshot.profile.display_name == "Demo Person");
  assert(snapshot.worklog.front().minutes == 420);

  office3ds::api::ProductDescriptor product;
  assert(product.api_version == 1);
  office3ds::api::DashboardLoadUpdate update;
  assert(update.total == 4);
  assert(update.result == office3ds::api::AdapterResult::ok);
  return 0;
}
