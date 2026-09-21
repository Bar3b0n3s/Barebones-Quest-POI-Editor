#include "app/EditorApp.h"

#if defined(_WIN32)
#include <Windows.h>
#include <objbase.h>
#else
#include <unistd.h>
#endif

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace
{
std::filesystem::path ExecutableDirectory()
{
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    auto const length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length)
        return std::filesystem::current_path();
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    std::string buffer(4096, '\0');
    auto const length = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length <= 0)
        return std::filesystem::current_path();
    buffer.resize(static_cast<std::size_t>(length));
    return std::filesystem::path(buffer).parent_path();
#endif
}

void ApplyTheme(float scale)
{
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 7.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.WindowPadding = { 12, 12 };
    style.FramePadding = { 8, 5 };
    style.ItemSpacing = { 8, 7 };
    style.ScaleAllSizes(scale);
    auto& colors = style.Colors;
    colors[ImGuiCol_WindowBg] = { 0.045f, 0.062f, 0.082f, 1.0f };
    colors[ImGuiCol_ChildBg] = { 0.06f, 0.08f, 0.105f, 1.0f };
    colors[ImGuiCol_FrameBg] = { 0.095f, 0.13f, 0.16f, 1.0f };
    colors[ImGuiCol_FrameBgHovered] = { 0.13f, 0.20f, 0.25f, 1.0f };
    colors[ImGuiCol_Button] = { 0.08f, 0.34f, 0.46f, 1.0f };
    colors[ImGuiCol_ButtonHovered] = { 0.10f, 0.46f, 0.62f, 1.0f };
    colors[ImGuiCol_ButtonActive] = { 0.08f, 0.55f, 0.72f, 1.0f };
    colors[ImGuiCol_Header] = { 0.08f, 0.32f, 0.43f, 1.0f };
    colors[ImGuiCol_HeaderHovered] = { 0.10f, 0.43f, 0.57f, 1.0f };
    colors[ImGuiCol_CheckMark] = { 0.34f, 0.82f, 1.0f, 1.0f };
    colors[ImGuiCol_Separator] = { 0.16f, 0.28f, 0.34f, 1.0f };
}

void SetWindowIcon(GLFWwindow* window, std::filesystem::path const& executableDirectory)
{
    auto const iconPath = executableDirectory / "quest-poi-editor-icon.png";
    int width = 0, height = 0, channels = 0;
    auto* pixels = stbi_load(iconPath.string().c_str(), &width, &height, &channels, 4);
    if (!pixels)
        return;
    GLFWimage image { width, height, pixels };
    glfwSetWindowIcon(window, 1, &image);
    stbi_image_free(pixels);
}

void LoadFont(std::filesystem::path const& executableDirectory, float scale)
{
    auto& io = ImGui::GetIO();
    auto const fontPath = executableDirectory / "AtkinsonHyperlegibleNext-Medium.ttf";
    if (!std::filesystem::is_regular_file(fontPath) ||
        !io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 17.0f * scale))
        io.Fonts->AddFontDefault();
}

int RunEditor()
{
#if defined(_WIN32)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
    if (!glfwInit())
    {
#if defined(_WIN32)
        CoUninitialize();
#endif
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    auto* window = glfwCreateWindow(1500, 900, "Barebones Quest POI Editor", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
#if defined(_WIN32)
        CoUninitialize();
#endif
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    auto const executableDirectory = ExecutableDirectory();
    SetWindowIcon(window, executableDirectory);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    float scaleX = 1.0f, scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    auto const interfaceScale = std::clamp(std::max(scaleX, scaleY), 1.0f, 2.5f);
    LoadFont(executableDirectory, interfaceScale);
    ApplyTheme(interfaceScale);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    {
        qpe::EditorApp app;
        while (!app.ExitReady())
        {
            glfwPollEvents();
            if (glfwWindowShouldClose(window))
            {
                app.RequestExit();
                glfwSetWindowShouldClose(window, GLFW_FALSE);
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            app.Render();
            ImGui::Render();

            int framebufferWidth = 0, framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClearColor(0.025f, 0.035f, 0.05f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
#if defined(_WIN32)
    CoUninitialize();
#endif
    return 0;
}
}

#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    return RunEditor();
}
#else
int main()
{
    return RunEditor();
}
#endif
