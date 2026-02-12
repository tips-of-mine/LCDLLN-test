#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/core/Time.h"
#include "engine/platform/FileSystem.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"

int main(int argc, char** argv) {
    engine::core::Config config;
    config.SetDefault("log.level", "info");
    config.SetDefault("log.file", "engine.log");
    config.SetDefault("log.flush", "true");
    config.SetDefault("app.window.title", "LCDLLN");
    config.SetDefault("app.window.width", "1280");
    config.SetDefault("app.window.height", "720");
    config.SetDefault("app.window.fullscreen", "false");
    config.SetDefault("paths.content", "game/data");

    config.LoadFromFile("config.json");
    config.ApplyCommandLine(argc, argv);

    engine::core::Log::Init({
        .minLevel = engine::core::LogLevel::Info,
        .filePath = config.GetString("log.file", "engine.log"),
        .flushAlways = config.GetBool("log.flush", true),
    });

    engine::platform::FileSystem fileSystem(config.GetString("paths.content", "game/data"));
    const auto contentEntries = fileSystem.List(".");
    LOG_INFO(Core, "Content root: ", config.GetString("paths.content", "game/data"));
    LOG_INFO(Core, "Entries in content root: ", static_cast<int>(contentEntries.size()));

    engine::platform::Window window;
    const engine::platform::WindowDesc windowDesc{
        .title = config.GetString("app.window.title", "LCDLLN"),
        .width = config.GetInt("app.window.width", 1280),
        .height = config.GetInt("app.window.height", 720),
        .fullscreen = config.GetBool("app.window.fullscreen", false),
    };

    if (!window.Create(windowDesc)) {
        LOG_ERROR(Core, "Unable to create platform window");
        engine::core::Log::Shutdown();
        return 1;
    }

    engine::platform::Input input;
    input.Attach(window);

    while (!window.ShouldClose()) {
        window.ResetFrameFlags();
        engine::core::Time::BeginFrame();

        window.PollEvents();
        input.BeginFrame();

        if (input.WasKeyPressed(engine::platform::Input::Key::Escape)) {
            window.RequestClose();
        }
        if (input.WasKeyPressed(engine::platform::Input::Key::F11)) {
            window.ToggleFullscreen();
        }

        if (window.WasResized()) {
            LOG_INFO(Core, "Window resized: ", window.Width(), "x", window.Height());
        }

        input.EndFrame();
        engine::core::Time::EndFrame();
    }

    input.Detach();
    window.Destroy();
    engine::core::Log::Shutdown();
    return 0;
}
