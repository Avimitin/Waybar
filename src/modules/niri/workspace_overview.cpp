#include "modules/niri/workspace_overview.hpp"

#include <algorithm>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <spdlog/spdlog.h>

#include "util/rewrite_string.hpp"

namespace waybar::modules::niri {

WorkspaceOverview::WorkspaceOverview(const std::string &id, const Bar &bar,
                                     const Json::Value &config)
    : AModule(config, "workspace-overview", id, false, false),
      bar_(bar),
      box_(bar.orientation, 0) {
  box_.set_name("workspace-overview");
  if (!id.empty()) {
    box_.get_style_context()->add_class(id);
  }
  box_.get_style_context()->add_class(MODULE_CLASS);
  event_box_.add(box_);

  if (!gIPC) gIPC = std::make_unique<IPC>();

  gIPC->registerForIPC("WindowsChanged", this);
  gIPC->registerForIPC("WindowOpenedOrChanged", this);
  gIPC->registerForIPC("WindowClosed", this);
  gIPC->registerForIPC("WindowFocusChanged", this);
  gIPC->registerForIPC("WorkspaceActivated", this);
  gIPC->registerForIPC("WorkspaceActiveWindowChanged", this);

  dp.emit();
}

WorkspaceOverview::~WorkspaceOverview() { gIPC->unregisterForIPC(this); }

void WorkspaceOverview::onEvent(const Json::Value &ev) { dp.emit(); }

void WorkspaceOverview::doUpdate() {
  auto ipcLock = gIPC->lockData();

  const auto &windows = gIPC->windows();
  const auto &workspaces = gIPC->workspaces();

  const auto separateOutputs = config_["separate-outputs"].asBool();
  const auto ws_it = std::find_if(workspaces.cbegin(), workspaces.cend(), [&](const auto &ws) {
    if (separateOutputs) {
      return ws["is_active"].asBool() && ws["output"].asString() == bar_.output->name;
    }
    return ws["is_focused"].asBool();
  });

  if (ws_it == workspaces.cend()) {
    // No active workspace, clear all buttons
    buttons_.clear();
    box_.show();
    return;
  }

  const auto workspace_id = (*ws_it)["id"].asUInt64();

  // Filter windows in the active workspace
  std::vector<Json::Value> workspace_windows;
  for (const auto &win : windows) {
    if (win["workspace_id"].asUInt64() == workspace_id) {
      workspace_windows.push_back(win);
    }
  }

  // Sort windows by scrolling layout position (column first, then row)
  // Floating windows are sorted last
  std::sort(workspace_windows.begin(), workspace_windows.end(),
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

  // Remove buttons for windows not in current workspace
  for (auto it = buttons_.begin(); it != buttons_.end();) {
    auto win = std::find_if(workspace_windows.begin(), workspace_windows.end(),
                            [it](const auto &w) { return w["id"].asUInt64() == it->first; });
    if (win == workspace_windows.end()) {
      it = buttons_.erase(it);
    } else {
      ++it;
    }
  }

  // Add buttons for new windows, update existing ones
  for (const auto &win : workspace_windows) {
    auto bit = buttons_.find(win["id"].asUInt64());
    auto &button = bit == buttons_.end() ? addButton(win) : bit->second;
    auto style_context = button.get_style_context();

    // Apply CSS classes based on window state
    if (win["is_focused"].asBool())
      style_context->add_class("focused");
    else
      style_context->remove_class("focused");

    if (win["is_floating"].asBool())
      style_context->add_class("floating");
    else
      style_context->remove_class("floating");

    // Set button label (app_id or format string)
    std::string label = win["app_id"].asString();
    if (config_["format"].isString()) {
      auto format = config_["format"].asString();
      label = fmt::format(fmt::runtime(format), fmt::arg("app_id", win["app_id"].asString()),
                          fmt::arg("title", win["title"].asString()));
    }

    // Apply rewrite rules to label
    label = waybar::util::rewriteString(label, config_["rewrite"]);

    if (!config_["disable-markup"].asBool()) {
      static_cast<Gtk::Label *>(button.get_children()[0])->set_markup(label);
    } else {
      button.set_label(label);
    }

    button.show();
  }

  // Reorder box children to match window order
  for (size_t i = 0; i < workspace_windows.size(); i++) {
    const auto win_id = workspace_windows[i]["id"].asUInt64();
    auto &button = buttons_.at(win_id);
    box_.reorder_child(button, i);
  }

  box_.show();
}

void WorkspaceOverview::update() {
  doUpdate();
  AModule::update();
}

Gtk::Button &WorkspaceOverview::addButton(const Json::Value &win) {
  std::string label = win["app_id"].asString();

  auto pair = buttons_.emplace(win["id"].asUInt64(), label);
  auto &&button = pair.first->second;
  box_.pack_start(button, false, false, 0);
  button.set_relief(Gtk::RELIEF_NONE);

  // Click handler to focus window
  if (!config_["disable-click"].asBool()) {
    const auto id = win["id"].asUInt64();
    button.signal_pressed().connect([id] {
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

  return button;
}

}  // namespace waybar::modules::niri
