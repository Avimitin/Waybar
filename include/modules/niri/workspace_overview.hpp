#pragma once

#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <json/value.h>
#include <unordered_map>

#include "AModule.hpp"
#include "bar.hpp"
#include "modules/niri/backend.hpp"
#include "util/icon_loader.hpp"

namespace waybar::modules::niri {

class WorkspaceOverview : public AModule, public EventHandler {
 public:
  WorkspaceOverview(const std::string &, const Bar &, const Json::Value &);
  ~WorkspaceOverview() override;
  void update() override;

 private:
  struct WindowButton {
    Gtk::Button button;
    Gtk::Box content_box;
    Gtk::Image icon;
    Gtk::Label label;
  };

  void onEvent(const Json::Value &ev) override;
  void doUpdate();
  WindowButton &addButton(const Json::Value &win);

  const Bar &bar_;
  Gtk::Box box_;
  IconLoader icon_loader_;
  // Map from niri window id to button.
  std::unordered_map<uint64_t, std::unique_ptr<WindowButton>> buttons_;
};

}  // namespace waybar::modules::niri
