#pragma once

#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <json/value.h>

#include "AModule.hpp"
#include "bar.hpp"
#include "modules/niri/backend.hpp"
#include "util/icon_loader.hpp"

namespace waybar::modules::niri {

class Workspaces : public AModule, public EventHandler {
 public:
  Workspaces(const std::string &, const Bar &, const Json::Value &);
  ~Workspaces() override;
  void update() override;

 private:
  struct WindowButton {
    Gtk::Button button;
    Gtk::Box content_box;
    Gtk::Image icon;
    Gtk::Label label;
  };

  struct WorkspaceGroup {
    Gtk::Box box;
    Gtk::Label label;
  };

  void onEvent(const Json::Value &ev) override;
  void doUpdate();
  Gtk::Button &addButton(const Json::Value &ws);
  WindowButton &addWindowButton(const Json::Value &win);
  std::string getIcon(const std::string &value, const Json::Value &ws);

  const Bar &bar_;
  Gtk::Box box_;
  IconLoader icon_loader_;
  // Map from niri workspace id to button.
  std::unordered_map<uint64_t, Gtk::Button> buttons_;
  // Map from niri workspace id to a box containing window buttons.
  std::unordered_map<uint64_t, std::unique_ptr<WorkspaceGroup>> workspace_groups_;
  // Map from niri window id to button.
  std::unordered_map<uint64_t, std::unique_ptr<WindowButton>> window_buttons_;
};

}  // namespace waybar::modules::niri
