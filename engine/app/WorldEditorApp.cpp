#include "engine/app/WorldEditorApp.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <string>
#include <string_view>

#if defined(_WIN32)
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#	include <GL/gl.h>
#elif defined(__APPLE__)
#	include <OpenGL/gl3.h>
#else
#	include <GL/gl.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#	define LCDLLN_IMGUI_GLSL_VERSION "#version 150"
#else
#	define LCDLLN_IMGUI_GLSL_VERSION "#version 330"
#endif

namespace engine::world_editor
{
	namespace
	{
		using namespace std::literals::string_view_literals;

		void LogOpt(const WorldEditorRunOptions& opts, std::string_view msg)
		{
			if (opts.log)
			{
				opts.log(msg);
			}
		}

		void GlfwErrorCallback(int code, const char* desc)
		{
			std::fprintf(stderr, "[world_editor][GLFW] %i: %s\n", code, desc ? desc : "");
			std::fflush(stderr);
		}
	} // namespace

	int RunWorldEditor(const WorldEditorRunOptions& opts)
	{
		LogOpt(opts, "GLFW: glfwSetErrorCallback + glfwInit"sv);
		glfwSetErrorCallback(GlfwErrorCallback);
		if (glfwInit() == GLFW_FALSE)
		{
			LogOpt(opts, "GLFW: glfwInit a échoué"sv);
			return 1;
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
#if defined(__APPLE__)
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

		LogOpt(opts, "GLFW: création fenêtre 1280x720 (LCDLLN World Editor)"sv);
		GLFWwindow* window = glfwCreateWindow(
			1280,
			720,
			"LCDLLN World Editor",
			nullptr,
			nullptr);
		if (!window)
		{
			LogOpt(opts, "GLFW: glfwCreateWindow a échoué"sv);
			glfwTerminate();
			return 1;
		}

		glfwMakeContextCurrent(window);
		glfwSwapInterval(1);

		LogOpt(opts, "OpenGL: contexte actif — vendeur / moteur / version (API 1.1)"sv);
		if (const GLubyte* v = glGetString(GL_VENDOR))
		{
			LogOpt(opts, std::string("OpenGL vendor: ").append(reinterpret_cast<const char*>(v)));
		}
		if (const GLubyte* r = glGetString(GL_RENDERER))
		{
			LogOpt(opts, std::string("OpenGL renderer: ").append(reinterpret_cast<const char*>(r)));
		}
		if (const GLubyte* ver = glGetString(GL_VERSION))
		{
			LogOpt(opts, std::string("OpenGL version: ").append(reinterpret_cast<const char*>(ver)));
		}

		LogOpt(opts, "ImGui: CreateContext + backends GLFW (OpenGL) + docking"sv);
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.IniFilename = "world_editor_imgui.ini";

		ImGui::StyleColorsDark();

		if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
		{
			LogOpt(opts, "ImGui: ImGui_ImplGlfw_InitForOpenGL a échoué"sv);
			ImGui::DestroyContext();
			glfwDestroyWindow(window);
			glfwTerminate();
			return 1;
		}

		if (!ImGui_ImplOpenGL3_Init(LCDLLN_IMGUI_GLSL_VERSION))
		{
			LogOpt(opts, "ImGui: ImGui_ImplOpenGL3_Init a échoué"sv);
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			glfwDestroyWindow(window);
			glfwTerminate();
			return 1;
		}

		LogOpt(opts, "ImGui: boucle principale (fermer la fenêtre ou Fichier → Quitter)"sv);

		while (glfwWindowShouldClose(window) == GLFW_FALSE)
		{
			glfwPollEvents();

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			if (ImGui::BeginMainMenuBar())
			{
				if (ImGui::BeginMenu("Fichier"))
				{
					if (ImGui::MenuItem("Quitter", "Alt+F4"))
					{
						glfwSetWindowShouldClose(window, GLFW_TRUE);
					}
					ImGui::EndMenu();
				}
				ImGui::EndMainMenuBar();
			}

			ImGui::Begin("Hub", nullptr, ImGuiWindowFlags_None);
			ImGui::TextUnformatted("Les Chroniques de la Lune Noire");
			ImGui::Separator();
			ImGui::TextUnformatted("World Editor — outil de production de données.");
			ImGui::BulletText("Sortie : fichiers sous game/data (aucun lien runtime avec lcdlln.exe).");
			ImGui::BulletText("Utilisez -log pour journaliser le démarrage (GLFW / OpenGL / ImGui).");
			ImGui::End();

			ImGui::Render();
			int display_w = 0;
			int display_h = 0;
			glfwGetFramebufferSize(window, &display_w, &display_h);
			glViewport(0, 0, display_w, display_h);
			glClearColor(0.06f, 0.06f, 0.09f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			glfwSwapBuffers(window);
		}

		LogOpt(opts, "ImGui / GLFW: arrêt propre"sv);
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		glfwDestroyWindow(window);
		glfwTerminate();
		return 0;
	}
} // namespace engine::world_editor
