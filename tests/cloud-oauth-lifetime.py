"""Exercise the shipped C++ callbacks with real deletion and AddressSanitizer.

Only rendering/network work is replaced by small doubles. The choice-page
function and cloudSetupPresent are extracted unchanged from the source under
test; this is a lifetime regression check, not a full device UI test.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

source_path = (Path(sys.argv[1]) if len(sys.argv) > 1 else
               Path(__file__).resolve().parents[1] / "es-app/src/guis/GuiMenu.cpp")
source = source_path.read_text()

def definition(name):
    start = source.index('static void ' + name + '(')
    while source.index(';', start) < source.index('{', start):
        start = source.index('static void ' + name + '(', start + 1)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]

harness = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#define _(value) std::string(value)
namespace Utils { namespace String {
std::string toUpper(const std::string& value) { return value; }
}}
namespace Renderer {
int getScreenWidth() { return 640; }
int getScreenHeight() { return 480; }
struct ScreenSettings { static bool fullScreenMenus() { return false; } };
}
struct GuiSettings;
struct Window {
    std::vector<GuiSettings*> stack;
    void pushGui(GuiSettings* gui) { stack.push_back(gui); }
};
struct Menu { void setSize(float, float) {} };
struct GuiSettings {
    Window* window;
    Menu menu;
    bool saveAllowed = true;
    std::vector<std::pair<std::string, std::function<void()>>> choices;
    GuiSettings(Window* w, const std::string&) : window(w) {}
    ~GuiSettings() {
        auto& stack = window->stack;
        stack.erase(std::remove(stack.begin(), stack.end(), this), stack.end());
    }
    void setSubTitle(const std::string&) {}
    void addGroup(const std::string&) {}
    Menu& getMenu() { return menu; }
    void addWithDescription(const std::string& label, const std::string&,
        std::nullptr_t, const std::function<void()>& action,
        const std::string&, bool, bool) { choices.emplace_back(label, action); }
    void save() { assert(saveAllowed); }
    void close() { save(); delete this; }
};
struct CloudBackend { std::string label = "Dropbox"; };
struct CloudOAuthReady { bool started = true; bool onDevice = true; };
static bool lastPhone;
static void cloudSetupSetButtons(GuiSettings*, std::nullptr_t) {}
'''
harness += '\n' + definition('cloudSetupPresent') + '\n'
harness += r'''
static void cloudOAuthShowSignIn(Window* window, const CloudBackend&,
    const std::string&, GuiSettings* prev, bool phone, const CloudOAuthReady&)
{
    lastPhone = phone;
    auto page = new GuiSettings(window, "Sign in");
    cloudSetupPresent(window, page, prev);
}
'''
harness += '\n' + definition('cloudOAuthPresentChoice') + '\n'
harness += r'''
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string route = argv[1];
    Window window;
    auto hub = new GuiSettings(&window, "Cloud hub");
    window.pushGui(hub);
    auto providers = new GuiSettings(&window, "Providers");
    window.pushGui(providers);
    CloudBackend backend;
    CloudOAuthReady ready;
    if (route == "no-browser") ready.onDevice = false;
    if (route == "failed-start") ready.started = false;
    cloudOAuthPresentChoice(&window, backend, "dropbox", providers, ready);
    assert(window.stack.size() == 2);
    assert(window.stack.front() == hub);
    if (route == "phone" || route == "keyboard") {
        auto chooser = window.stack.back();
        const std::string label = route == "phone"
            ? "WITH MY PHONE" : "WITH THE ON-SCREEN KEYBOARD";
        auto row = std::find_if(chooser->choices.begin(), chooser->choices.end(),
            [&label](const auto& entry) { return entry.first == label; });
        assert(row != chooser->choices.end());
        // ES event dispatch can outlive the page being replaced. Keep the
        // invocation alive while testing what page its callback hands over.
        const auto action = row->second;
        action();
        assert(window.stack.size() == 2);
        assert(window.stack.front() == hub);
        assert(window.stack.back() != chooser);
        assert(lastPhone == (route == "phone"));
    } else {
        assert(lastPhone);
    }
    // Back from sign-in must return directly to the hub.
    window.stack.back()->close();
    assert(window.stack.size() == 1 && window.stack.back() == hub);
    hub->close();
    assert(window.stack.empty());
    std::cout << "PASS " << route << '\n';
}
'''

scratch = tempfile.TemporaryDirectory(prefix='cloud-oauth-lifetime-')
folder = Path(scratch.name)
(folder / 'lifetime.cpp').write_text(harness)
subprocess.run(['g++', '-std=c++14', '-O1', '-g', '-fsanitize=address',
                '-fno-omit-frame-pointer', str(folder / 'lifetime.cpp'),
                '-o', str(folder / 'lifetime')], check=True)
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1')
failures = 0
for route in ('phone', 'keyboard', 'no-browser', 'failed-start'):
    run = subprocess.run([str(folder / 'lifetime'), route],
                         capture_output=True, text=True, env=env)
    (folder / (route + '.log')).write_text(run.stdout + run.stderr)
    if run.returncode:
        failures += 1
        print('FAIL ' + route)
        print('\n'.join(run.stderr.splitlines()[:15]))
    else:
        print(run.stdout.strip())
sys.exit(1 if failures else 0)
