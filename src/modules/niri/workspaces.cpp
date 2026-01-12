#include "modules/niri/workspaces.hpp"

#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <spdlog/spdlog.h>

#include "util/rewrite_string.hpp"

namespace waybar::modules::niri {

Workspaces::Workspaces(const std::string &id, const Bar &bar, const Json::Value &config)
    : AModule(config, "workspaces", id, false, false), bar_(bar), box_(bar.orientation, 0) {
  box_.set_name("workspaces");
  if (!id.empty()) {
    box_.get_style_context()->add_class(id);
  }
  box_.get_style_context()->add_class(MODULE_CLASS);
  event_box_.add(box_);

  if (!gIPC) gIPC = std::make_unique<IPC>();

  gIPC->registerForIPC("WorkspacesChanged", this);
  gIPC->registerForIPC("WorkspaceActivated", this);
  gIPC->registerForIPC("WorkspaceActiveWindowChanged", this);
  gIPC->registerForIPC("WorkspaceUrgencyChanged", this);
  gIPC->registerForIPC("WindowsChanged", this);
  gIPC->registerForIPC("WindowOpenedOrChanged", this);
  gIPC->registerForIPC("WindowClosed", this);
  gIPC->registerForIPC("WindowFocusChanged", this);

  if (config_["icon-theme"].isArray()) {
    for (auto &c : config_["icon-theme"]) {
      icon_loader_.add_custom_icon_theme(c.asString());
    }
  } else if (config_["icon-theme"].isString()) {
    icon_loader_.add_custom_icon_theme(config_["icon-theme"].asString());
  }

  dp.emit();
}

Workspaces::~Workspaces() { gIPC->unregisterForIPC(this); }

void Workspaces::onEvent(const Json::Value &ev) { dp.emit(); }

void Workspaces::doUpdate() {
  auto ipcLock = gIPC->lockData();

  const auto alloutputs = config_["all-outputs"].asBool();
  const auto show_icons = config_["app-icons"].asBool();
  std::vector<Json::Value> my_workspaces;
  const auto &workspaces = gIPC->workspaces();
  std::copy_if(workspaces.cbegin(), workspaces.cend(), std::back_inserter(my_workspaces),
               [&](const auto &ws) {
                 if (alloutputs) return true;
                 return ws["output"].asString() == bar_.output->name;
               });

  // Find active workspace
  uint64_t active_ws_id = 0;
  bool has_active_ws = false;
  for (const auto &ws : my_workspaces) {
    if ((!alloutputs && ws["is_active"].asBool()) || (alloutputs && ws["is_focused"].asBool())) {
       active_ws_id = ws["id"].asUInt64();
       has_active_ws = true;
       break;
    }
  }

  // Get windows for active workspace if icons enabled
  std::vector<Json::Value> active_windows;
  if (show_icons && has_active_ws) {
      const auto &windows = gIPC->windows();
      for (const auto &win : windows) {
          if (win["workspace_id"].asUInt64() == active_ws_id) {
              active_windows.push_back(win);
          }
      }

      // Sort windows (same as WorkspaceOverview)
      std::sort(active_windows.begin(), active_windows.end(),
            [](const auto &a, const auto &b) {
              const auto a_floating = a["is_floating"].asBool();
              const auto b_floating = b["is_floating"].asBool();

              // Floating windows go to the end
              if (a_floating && !b_floating) return false;
              if (!a_floating && b_floating) return true;

              // Check if windows have layout.pos_in_scrolling_layout
              const auto &a_layout = a["layout"]["pos_in_scrolling_layout"];
              const auto &b_layout = b["layout"]["pos_in_scrolling_layout"];

              // Windows without layout info go to the end
              if (a_layout.isNull() && b_layout.isNull()) return false;
              if (a_layout.isNull()) return false;
              if (b_layout.isNull()) return true;

              // Both have layout info - sort by column first, then row
              const auto a_col = a_layout[0].asInt();
              const auto a_row = a_layout[1].asInt();
              const auto b_col = b_layout[0].asInt();
              const auto b_row = b_layout[1].asInt();

              if (a_col != b_col) return a_col < b_col;
              return a_row < b_row;
            });
  }

  // Remove buttons for removed workspaces OR active workspace if showing icons AND has windows
  for (auto it = buttons_.begin(); it != buttons_.end();) {
    auto ws = std::find_if(my_workspaces.begin(), my_workspaces.end(),
                           [it](const auto &ws) { return ws["id"].asUInt64() == it->first; });
    
    bool should_remove = (ws == my_workspaces.end());
    if (!should_remove && show_icons && it->first == active_ws_id && !active_windows.empty()) {
        should_remove = true;
    }

    if (should_remove) {
      it = buttons_.erase(it);
    } else {
      ++it;
    }
  }

  // Remove window buttons that are no longer present
  for (auto it = window_buttons_.begin(); it != window_buttons_.end();) {
      auto win = std::find_if(active_windows.begin(), active_windows.end(),
                              [it](const auto &w) { return w["id"].asUInt64() == it->first; });
      if (win == active_windows.end()) {
          it = window_buttons_.erase(it);
      } else {
          ++it;
      }
  }
  
  // Update/Add Workspace Buttons
  for (const auto &ws : my_workspaces) {
      uint64_t id = ws["id"].asUInt64();
      if (show_icons && id == active_ws_id && !active_windows.empty()) continue;

    auto bit = buttons_.find(id);
    auto &button = bit == buttons_.end() ? addButton(ws) : bit->second;
    auto style_context = button.get_style_context();

    if (ws["is_focused"].asBool())
      style_context->add_class("focused");
    else
      style_context->remove_class("focused");

    if (ws["is_active"].asBool())
      style_context->add_class("active");
    else
      style_context->remove_class("active");

    if (ws["is_urgent"].asBool())
      style_context->add_class("urgent");
    else
      style_context->remove_class("urgent");

    if (ws["output"]) {
      if (ws["output"].asString() == bar_.output->name)
        style_context->add_class("current_output");
      else
        style_context->remove_class("current_output");
    } else {
      style_context->remove_class("current_output");
    }

    if (ws["active_window_id"].isNull())
      style_context->add_class("empty");
    else
      style_context->remove_class("empty");

    std::string name;
    if (ws["name"]) {
      style_context->remove_class("unnamed");
      name = ws["name"].asString();
    } else {
      style_context->add_class("unnamed");
      name = std::to_string(ws["idx"].asUInt());
    }
    button.set_name("niri-workspace-" + name);

    if (config_["format"].isString()) {
      auto format = config_["format"].asString();
      name = fmt::format(fmt::runtime(format), fmt::arg("icon", getIcon(name, ws)),
                         fmt::arg("value", name), fmt::arg("name", ws["name"].asString()),
                         fmt::arg("index", ws["idx"].asUInt()),
                         fmt::arg("output", ws["output"].asString()));
    }
    if (!config_["disable-markup"].asBool()) {
      static_cast<Gtk::Label *>(button.get_children()[0])->set_markup(name);
    } else {
      button.set_label(name);
    }

    if (config_["current-only"].asBool()) {
      const auto *property = alloutputs ? "is_focused" : "is_active";
      if (ws[property].asBool())
        button.show();
      else
        button.hide();
    } else {
      button.show();
    }
  }

  // Update/Add Window Buttons
  for (const auto &win : active_windows) {
    auto bit = window_buttons_.find(win["id"].asUInt64());
    auto &button_struct = bit == window_buttons_.end() ? addWindowButton(win) : *bit->second;
    auto &button = button_struct.button;
    auto style_context = button.get_style_context();

    style_context->add_class("app-icon");

    if (win["is_focused"].asBool())
      style_context->add_class("focused");
    else
      style_context->remove_class("focused");

    if (win["is_floating"].asBool())
      style_context->add_class("floating");
    else
      style_context->remove_class("floating");

    std::string label = win["app_id"].asString();
    if (config_["format-window"].isString()) {
      auto format = config_["format-window"].asString();
      label = fmt::format(fmt::runtime(format), fmt::arg("title", win["title"].asString()),
                          fmt::arg("app_id", win["app_id"].asString()));
    }

    label = waybar::util::rewriteString(label, config_["rewrite"]);

    if (!config_["disable-markup"].asBool()) {
      button_struct.label.set_markup(label);
    } else {
      button_struct.label.set_text(label);
    }
    
    if (label.empty()) {
        button_struct.label.hide();
    } else {
        button_struct.label.show();
    }

    // Icon handling
    if (config_["icon"].isBool() ? config_["icon"].asBool() : true) {
       int icon_size = config_["icon-size"].isUInt() ? config_["icon-size"].asUInt() : 24;
       auto app_id = win["app_id"].asString();
       auto app_info = icon_loader_.get_app_info_from_app_id_list(app_id);
       if (icon_loader_.image_load_icon(button_struct.icon, app_info, icon_size)) {
           button_struct.icon.show();
       } else {
           button_struct.icon.hide();
       }
    } else {
        button_struct.icon.hide();
    }

    button.show();
  }

  // Final Reorder
  int child_idx = 0;
  // We iterate through workspaces in order
  std::vector<std::pair<std::string, uint64_t>> nameIdPairs;
  for (auto &ws : workspaces) {
      if (alloutputs || ws["output"].asString() == bar_.output->name) {
          uint64_t id = ws["id"].asUInt64();
          std::string name;
          if (ws["name"].isNull()) name = "ZZZ";
          else name = ws["name"].asString();
          nameIdPairs.emplace_back(name, id);
      }
  }
  std::sort(nameIdPairs.begin(), nameIdPairs.end());

  for (const auto &pair : nameIdPairs) {
      uint64_t ws_id = pair.second;
      if (show_icons && ws_id == active_ws_id && !active_windows.empty()) {
          // Add window buttons
          for (const auto &win : active_windows) {
              uint64_t win_id = win["id"].asUInt64();
              if (window_buttons_.contains(win_id)) {
                  box_.reorder_child(window_buttons_.at(win_id)->button, child_idx++);
              }
          }
      } else {
          // Add workspace button
          if (buttons_.contains(ws_id)) {
              box_.reorder_child(buttons_.at(ws_id), child_idx++);
          }
      }
  }
}

void Workspaces::update() {
  doUpdate();
  AModule::update();
}

Gtk::Button &Workspaces::addButton(const Json::Value &ws) {
  std::string name;
  if (ws["name"]) {
    name = ws["name"].asString();
  } else {
    name = std::to_string(ws["idx"].asUInt());
  }

  auto pair = buttons_.emplace(ws["id"].asUInt64(), name);
  auto &&button = pair.first->second;
  box_.pack_start(button, false, false, 0);
  button.set_relief(Gtk::RELIEF_NONE);
  if (!config_["disable-click"].asBool()) {
    const auto id = ws["id"].asUInt64();
    button.signal_pressed().connect([=] {
      try {
        // {"Action":{"FocusWorkspace":{"reference":{"Id":1}}}}
        Json::Value request(Json::objectValue);
        auto &action = (request["Action"] = Json::Value(Json::objectValue));
        auto &focusWorkspace = (action["FocusWorkspace"] = Json::Value(Json::objectValue));
        auto &reference = (focusWorkspace["reference"] = Json::Value(Json::objectValue));
        reference["Id"] = id;

        IPC::send(request);
      } catch (const std::exception &e) {
        spdlog::error("Error switching workspace: {}", e.what());
      }
    });
  }
  return button;
}

Workspaces::WindowButton &Workspaces::addWindowButton(const Json::Value &win) {
  auto wb = std::make_unique<WindowButton>();
  
  wb->content_box.set_orientation(Gtk::ORIENTATION_HORIZONTAL);
  wb->content_box.set_spacing(4);
  wb->content_box.pack_start(wb->icon, false, false, 0);
  wb->content_box.pack_start(wb->label, false, false, 0);
  wb->content_box.show();
  
  wb->button.add(wb->content_box);
  wb->button.set_relief(Gtk::RELIEF_NONE);

  // Click handler to focus window
  if (!config_["disable-click"].asBool()) {
    const auto id = win["id"].asUInt64();
    wb->button.signal_pressed().connect([id] {
      try {
        // {"Action":{"FocusWindow":{"id":window_id}}}
        Json::Value request(Json::objectValue);
        auto &action = (request["Action"] = Json::Value(Json::objectValue));
        auto &focusWindow = (action["FocusWindow"] = Json::Value(Json::objectValue));
        focusWindow["id"] = id;

        IPC::send(request);
      } catch (const std::exception &e) {
        spdlog::error("Error focusing window: {}", e.what());
      }
    });
  }

  auto res = window_buttons_.emplace(win["id"].asUInt64(), std::move(wb));
  WindowButton &inserted_wb = *res.first->second;
  box_.pack_start(inserted_wb.button, false, false, 0);
  
  return inserted_wb;
}

std::string Workspaces::getIcon(const std::string &value, const Json::Value &ws) {
  const auto &icons = config_["format-icons"];
  if (!icons) return value;

  if (ws["is_urgent"].asBool() && icons["urgent"]) return icons["urgent"].asString();

  if (ws["active_window_id"].isNull() && icons["empty"]) return icons["empty"].asString();

  if (ws["is_focused"].asBool() && icons["focused"]) return icons["focused"].asString();

  if (ws["is_active"].asBool() && icons["active"]) return icons["active"].asString();

  if (ws["name"]) {
    const auto &name = ws["name"].asString();
    if (icons[name]) return icons[name].asString();
  }

  const auto idx = ws["idx"].asString();
  if (icons[idx]) return icons[idx].asString();

  if (icons["default"]) return icons["default"].asString();

  return value;
}

}  // namespace waybar::modules::niri
