#pragma once

#include <gtkmm/button.h>
#include <json/value.h>
#include <unordered_map>

#include "AModule.hpp"
#include "bar.hpp"
#include "modules/niri/backend.hpp"

namespace waybar::modules::niri {

class WorkspaceOverview : public AModule, public EventHandler {
 public:
  WorkspaceOverview(const std::string &, const Bar &, const Json::Value &);
  ~WorkspaceOverview() override;
  void update() override;

 private:
  void onEvent(const Json::Value &ev) override;
  void doUpdate();
  Gtk::Button &addButton(const Json::Value &win);

  const Bar &bar_;
  Gtk::Box box_;
  // Map from niri window id to button.
  std::unordered_map<uint64_t, Gtk::Button> buttons_;
};

}  // namespace waybar::modules::niri
