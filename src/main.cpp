#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

// For Converting Strings To LowerCase in FixFileExtension function
#include <algorithm>
#include <cctype>

#include <chrono>
#include <thread>

#include "glad/glad.h"
#include "GLFW/glfw3.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "tinyfiledialogs.h"

#include "main.h"
#include "save.h"
#include "macros.h"
#include "assets.h"
#include "shader.h"
#include "helpers.h"
#include "palette.h"

std::string FilePath = "untitled.png"; // Default Output Filename
char const * FileFilterPatterns[3] = { "*.png", "*.jpg", "*.jpeg" };
unsigned int NumOfFilterPatterns = 3;

int WindowDims[2] = {700, 500}; // Default Window Dimensions
int CanvasDims[2] = {60, 40}; // Width, Height Default Canvas Size

uint8_t *CanvasData = NULL; // Canvas Data Containg Pixel Values.
#define CANVAS_SIZE_B CanvasDims[0] * CanvasDims[1] * 4 * sizeof(uint8_t)

unsigned int ZoomLevel = 8; // Default Zoom Level
std::string ZoomText = "Zoom: " + std::to_string(ZoomLevel) + "x"; // Human Readable string decribing zoom level for UI
unsigned char BrushSize = 5; // Default Brush Size

enum tool_e { BRUSH, ERASER, PAN, FILL, INK_DROPPER, LINE, RECTANGLE };
enum mode_e { SQUARE, CIRCLE };

// Currently & last selected tool
enum tool_e Tool = BRUSH;
enum tool_e LastTool = BRUSH;
enum mode_e Mode = CIRCLE;
enum mode_e LastMode = CIRCLE;

bool IsCtrlDown = false;
bool IsShiftDown = false;
bool CanvasFreeze = false;
bool ShouldSave = false;
bool ImgDidChange = false;
bool LMB_Pressed = false;
bool ShowNewCanvasWindow = false; // Holds Whether to show new canvas window or not.

unsigned int LastPaletteIndex = 0;
unsigned int PaletteIndex = 0;
palette_t* P = NULL;

#define SelectedColor P->entries[PaletteIndex]

GLfloat ViewPort[4];
GLfloat CanvasVertices[] = {
	//       Canvas              Color To       Texture
	//     Coordinates          Blend With     Coordinates
	//  X      Y      Z      R     G     B      X     Y
	   1.0f,  1.0f,  0.0f,  1.0f, 1.0f, 1.0f,  1.0f, 0.0f, // Top Right
	   1.0f, -1.0f,  0.0f,  1.0f, 1.0f, 1.0f,  1.0f, 1.0f, // Bottom Right
	  -1.0f, -1.0f,  0.0f,  1.0f, 1.0f, 1.0f,  0.0f, 1.0f, // Bottom Left
	  -1.0f,  1.0f,  0.0f,  1.0f, 1.0f, 1.0f,  0.0f, 0.0f  // Top Left
	// Z Coordinates Are 0 Because We Are Working With 2D Stuff
	// Color To Blend With Are The Colors Which Will Be multiplied by the selected color to get the final output on the canvas
};

// Index Buffer
unsigned int Indices[] = {0, 1, 3, 1, 2, 3};

struct cvstate {
	unsigned char* pixels;
	cvstate* next;
	cvstate* prev;
};

struct mousepos {
	double X;
	double Y;
	double LastX;
	double LastY;
	double DownX;
	double DownY;
};

typedef struct cvstate cvstate_t;
typedef struct mousepos mousepos_t;

cvstate_t* CurrentState = NULL;

mousepos_t MousePos = { 0 };
mousepos_t MousePosRel = { 0 };

int main(int argc, char **argv) {
	for (unsigned char i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-f") == 0) {
			FilePath = argv[i+1];
			LoadImageToCanvas(FilePath.c_str(), CanvasDims, &CanvasData);
			i++;
		}

		if (strcmp(argv[i], "-d") == 0) {
			int w, h;
			string_to_int(&w, argv[i + 1]);
			string_to_int(&h, argv[i + 2]);
			CanvasDims[0] = w;
			CanvasDims[1] = h;
			i += 2;
		}

		if (strcmp(argv[i], "-o") == 0) {
			FilePath = argv[i + 1];
			i++;
		}

		if (strcmp(argv[i], "-w") == 0) {
			int w, h;
			string_to_int(&w, argv[i + 1]);
			string_to_int(&h, argv[i + 2]);
			WindowDims[0] = w;
			WindowDims[1] = h;
			i += 2;
		}
#if 0
		if (strcmp(argv[i], "-p") == 0) {
			PaletteIndex = 0;
			i++;

			while (i < argc && (strlen(argv[i]) == 6 || strlen(argv[i]) == 8)) {
				long number = (long)strtol(argv[i], NULL, 16);
				int start;
				unsigned char r, g, b, a;

				if (strlen(argv[i]) == 6) {
					start = 16;
					a = 255;
				} else if (strlen(argv[i]) == 8) {
					start = 24;
					a = number >> (start - 24) & 0xff;
				} else {
					printf("Invalid color in P, check the length is 6 or 8.\n");
					break;
				}

				r = number >> start & 0xff;
				g = number >> (start - 8) & 0xff;
				b = number >> (start - 16) & 0xff;

				P[PaletteIndex + 1][0] = r;
				P[PaletteIndex + 1][1] = g;
				P[PaletteIndex + 1][2] = b;
				P[PaletteIndex + 1][3] = a;

				printf("Adding color: #%s - rgb(%d, %d, %d)\n", argv[i], r, g, b);

				PaletteIndex++;
				i++;
			}
		}
#endif
	}

	if (CanvasData == NULL) {
		CanvasData = (unsigned char *)malloc(CANVAS_SIZE_B);
		memset(CanvasData, 0, CANVAS_SIZE_B);
		if (CanvasData == NULL) {
			printf("Unable To allocate memory for canvas.\n");
			return 1;
		}
	}

	P = LoadCsvPalette((const char*)assets_get("data/palettes/cc-29.csv", NULL));

	GLFWwindow *window;
	GLFWcursor *cursor;

	glfwInit();
	glfwSetErrorCallback(logGLFWErrors);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

#ifdef __APPLE__
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE); // We're Using OpenGL 3.0 and that's why don't requrest for any profile
#endif

	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_TRUE);

	window = glfwCreateWindow(WindowDims[0], WindowDims[1], "csprite", NULL, NULL);
	cursor = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
	glfwSetCursor(window, cursor);

	if (!window) {
		printf("Failed to create GLFW window\n");
		free(CanvasData);
		free(P);
		return 1;
	}

	glfwMakeContextCurrent(window);
	glfwSetWindowTitle(window, WINDOW_TITLE_CSTR);
	glfwSwapInterval(0);

	// Conditionally Enable/Disable Window icon to reduce compile time in debug Tool.
#ifdef ENABLE_WIN_ICON
	GLFWimage iconArr[3];
	iconArr[0].width = 16;
	iconArr[0].height = 16;
	iconArr[0].pixels = (unsigned char*)assets_get("data/icons/icon-16.png", NULL);

	iconArr[1].width = 32;
	iconArr[1].height = 32;
	iconArr[1].pixels = (unsigned char*)assets_get("data/icons/icon-32.png", NULL);

	iconArr[2].width = 48;
	iconArr[2].height = 48;
	iconArr[2].pixels = (unsigned char*)assets_get("data/icons/icon-48.png", NULL);
	glfwSetWindowIcon(window, 3, iconArr);
#endif

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		printf("Failed to init GLAD\n");
		free(CanvasData);
		free(P);
		return 1;
	}

	// Initial Canvas Position
	ViewPort[0] = (float)WindowDims[0] / 2 - (float)CanvasDims[0] * ZoomLevel / 2; // X Position
	ViewPort[1] = (float)WindowDims[1] / 2 - (float)CanvasDims[1] * ZoomLevel / 2; // Y Position

	// Output Width And Height Of The Canvas
	ViewPort[2] = CanvasDims[0] * ZoomLevel; // Width
	ViewPort[3] = CanvasDims[1] * ZoomLevel; // Height

	ZoomNLevelViewport();
	glfwSetWindowSizeCallback(window, WindowSizeCallback);
	glfwSetFramebufferSizeCallback(window, FrameBufferSizeCallback);
	glfwSetScrollCallback(window, ScrollCallback);
	glfwSetKeyCallback(window, KeyCallback);
	glfwSetMouseButtonCallback(window, MouseButtonCallback);

	unsigned int shader_program = CreateShaderProgram(
#ifdef __APPLE__
		"data/shaders/vertex_33.glsl",
		"data/shaders/fragment_33.glsl",
#else
		"data/shaders/vertex.glsl",
		"data/shaders/fragment.glsl",
#endif
		NULL
	);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	unsigned int vertexBuffObj, vertexArrObj, ebo;
	glGenVertexArrays(1, &vertexArrObj);
	glGenBuffers(1, &vertexBuffObj);
	glGenBuffers(1, &ebo);
	glBindVertexArray(vertexArrObj);

	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffObj);
	glBufferData(GL_ARRAY_BUFFER, sizeof(CanvasVertices), CanvasVertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Indices), Indices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));
	glEnableVertexAttribArray(2);

#ifndef __APPLE__
	// We Bind Attrib Locations Using These Functions Because OpenGL 3.0 Didn't Support Layouts
	glBindAttribLocation(shader_program, 0, "position");
	glBindAttribLocation(shader_program, 1, "color");
	glBindAttribLocation(shader_program, 2, "tex_coords");
#endif

	unsigned int texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CanvasDims[0], CanvasDims[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, CanvasData);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.IniFilename = NULL; // Disable Generation of .ini file

	const void* Montserrat_Bold = NULL;
	int Montserrat_Bold_Size = 0;
	ImDrawList* ImGuiDrawList = NULL;

	Montserrat_Bold = assets_get("data/fonts/Montserrat-Bold.ttf", &Montserrat_Bold_Size);

	io.Fonts->AddFontFromMemoryCompressedTTF(Montserrat_Bold, Montserrat_Bold_Size, 16.0f);

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __APPLE__
	ImGui_ImplOpenGL3_Init("#version 330");
#else
	ImGui_ImplOpenGL3_Init("#version 130");
#endif

	ImGuiWindowFlags window_flags = 0;
	window_flags |= ImGuiWindowFlags_NoBackground;
	window_flags |= ImGuiWindowFlags_NoTitleBar;
	window_flags |= ImGuiWindowFlags_NoResize;
	window_flags |= ImGuiWindowFlags_NoMove;

#ifdef SHOW_FRAME_TIME
	double lastTime = glfwGetTime();
	int nbFrames = 0; // Number Of Frames Rendered
#endif

	auto const wait_time = std::chrono::milliseconds{ 17 };
	auto const start_time = std::chrono::steady_clock::now();
	auto next_time = start_time + wait_time;

	int NEW_DIMS[2] = {60, 40}; // Default Width, Height New Canvas if Created One

	SaveState(); // Save The Inital State

	while (!glfwWindowShouldClose(window)) {
		// --------------------------------------------------------------------------------------
		// Updating Cursor Position Here because function callback was causing performance issues.
		glfwGetCursorPos(window, &MousePos.X, &MousePos.Y);
		/* infitesimally small chance aside from startup */
		if (MousePos.LastX != 0 && MousePos.LastY != 0) {
			if (Tool == PAN) {
				ViewPort[0] -= MousePos.LastX - MousePos.X;
				ViewPort[1] += MousePos.LastY - MousePos.Y;
				ViewportSet();
			}
		}
		MousePos.LastX = MousePos.X;
		MousePos.LastY = MousePos.Y;

		MousePosRel.LastX = MousePosRel.X;
		MousePosRel.LastY = MousePosRel.Y;
		MousePosRel.X = MousePos.X - ViewPort[0];
		MousePosRel.Y = (MousePos.Y + ViewPort[1]) - (WindowDims[1] - ViewPort[3]);
		// --------------------------------------------------------------------------------------

#ifdef SHOW_FRAME_TIME
		double currentTime = glfwGetTime();
		nbFrames++;
		if (currentTime - lastTime >= 1.0) {
			// printf("%f ms/frame\n", 1000.0 / double(nbFrames));
			nbFrames = 0;
			lastTime += 1.0;
		}
#endif

		std::this_thread::sleep_until(next_time);

		glfwPollEvents();
		ProcessInput(window);

		glClearColor(0.075, 0.075, 0.1, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(shader_program);
		glBindVertexArray(vertexArrObj);
		glBindTexture(GL_TEXTURE_2D, 0);

		unsigned int patternSize_loc = glGetUniformLocation(shader_program, "patternSize");
		glUniform1f(patternSize_loc, ZoomLevel);

		unsigned int alpha_loc = glGetUniformLocation(shader_program, "alpha");
		glUniform1f(alpha_loc, 0.2f);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

		glUniform1f(alpha_loc, 1.0f);
		glBindTexture(GL_TEXTURE_2D, texture);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CanvasDims[0], CanvasDims[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, CanvasData);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("New", "Ctrl+N")) {
					ShowNewCanvasWindow = 1;
					CanvasFreeze = true;
				}
				if (ImGui::MenuItem("Open", "Ctrl+O")) {
					char *filePath = tinyfd_openFileDialog("Open A File", NULL, NumOfFilterPatterns, FileFilterPatterns, "Image File (.png, .jpg, .jpeg)", 0);
					if (filePath != NULL) {
						FilePath = std::string(filePath);
						LoadImageToCanvas(FilePath.c_str(), CanvasDims, &CanvasData);
						glfwSetWindowTitle(window, WINDOW_TITLE_CSTR);
						ZoomNLevelViewport();
					}
				}
				if (ImGui::BeginMenu("Save")) {
					if (ImGui::MenuItem("Save", "Ctrl+S")) {
						FilePath = FixFileExtension(FilePath);
						SaveImageFromCanvas(FilePath);
						glfwSetWindowTitle(window, WINDOW_TITLE_CSTR);
						FreeHistory();
						SaveState();
					}
					if (ImGui::MenuItem("Save As", "Alt+S")) {
						char *filePath = tinyfd_saveFileDialog("Save A File", NULL, NumOfFilterPatterns, FileFilterPatterns, "Image File (.png, .jpg, .jpeg)");
						if (filePath != NULL) {
							FilePath = FixFileExtension(std::string(filePath));
							SaveImageFromCanvas(FilePath);
							glfwSetWindowTitle(window, WINDOW_TITLE_CSTR);
							FreeHistory();
							SaveState();
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Edit")) {
				if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
					Undo();
				}
				if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
					Redo();
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Help")) {
				if (ImGui::MenuItem("About")) {
					OpenURL("https://github.com/pegvin/CSprite/wiki/About-CSprite");
				}
				if (ImGui::MenuItem("GitHub")) {
					OpenURL("https://github.com/pegvin/CSprite");
				}
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		if (ShowNewCanvasWindow) {
			ImGui::SetNextWindowSize({230, 100}, 0);
			if (ImGui::BeginPopupModal(
					"ShowNewCanvasWindow",
					NULL,
					ImGuiWindowFlags_NoCollapse |
					ImGuiWindowFlags_NoTitleBar |
					ImGuiWindowFlags_NoResize   |
					ImGuiWindowFlags_NoMove
			)) {
				ImGui::InputInt("width", &NEW_DIMS[0], 1, 1, 0);
				ImGui::InputInt("height", &NEW_DIMS[1], 1, 1, 0);

				if (ImGui::Button("Ok")) {
					FreeHistory();
					free(CanvasData);
					CanvasDims[0] = NEW_DIMS[0];
					CanvasDims[1] = NEW_DIMS[1];
					CanvasData = (unsigned char *)malloc(CANVAS_SIZE_B);
					memset(CanvasData, 0, CANVAS_SIZE_B);
					if (CanvasData == NULL) {
						printf("Unable To allocate memory for canvas.\n");
						return 1;
					}

					ZoomNLevelViewport();
					CanvasFreeze = false;
					ShowNewCanvasWindow = false;
					SaveState();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					CanvasFreeze = false;
					ShowNewCanvasWindow = false;
				}
				ImGui::EndPopup();
			} else {
				ImGui::OpenPopup("ShowNewCanvasWindow");
			}
		}

		if (ImGui::Begin("ToolAndZoomWindow", NULL, window_flags | ImGuiWindowFlags_NoBringToFrontOnFocus |  ImGuiWindowFlags_NoFocusOnAppearing)) {
			ImGui::SetWindowPos({0, 20});
			std::string selectedToolText;

			switch (Tool) {
				case BRUSH:
					if (Mode == SQUARE)
						selectedToolText = "Square Brush - (Size: " + std::to_string(BrushSize) + ")";
					else
						selectedToolText = "Circle Brush - (Size: " + std::to_string(BrushSize) + ")";
					break;
				case ERASER:
					if (Mode == SQUARE)
						selectedToolText = "Square Eraser - (Size: " + std::to_string(BrushSize) + ")";
					else
						selectedToolText = "Circle Eraser - (Size: " + std::to_string(BrushSize) + ")";
					break;
				case FILL:
					selectedToolText = "Fill";
					break;
				case INK_DROPPER:
					selectedToolText = "Ink Dropper";
					break;
				case PAN:
					selectedToolText = "Panning";
					break;
				case LINE:
					if (Mode == SQUARE)
						selectedToolText = "Square Line - (Size: " + std::to_string(BrushSize) + ")";
					else
						selectedToolText = "Round Line - (Size: " + std::to_string(BrushSize) + ")";
					break;
				case RECTANGLE:
					if (Mode == SQUARE)
						selectedToolText = "Square Rect - (Size: " + std::to_string(BrushSize) + ")";
					else
						selectedToolText = "Round Rect - (Size: " + std::to_string(BrushSize) + ")";
					break;
			}

			ImVec2 textSize1 = ImGui::CalcTextSize(selectedToolText.c_str(), NULL, false, -2.0f);
			ImVec2 textSize2 = ImGui::CalcTextSize(ZoomText.c_str(), NULL, false, -2.0f);
			ImGui::SetWindowSize({(float)(textSize1.x + textSize2.x), (float)(textSize1.y + textSize2.y) * 2}); // Make Sure Text is visible everytime.

			ImGui::Text("%s", selectedToolText.c_str());
			ImGui::Text("%s", ZoomText.c_str());
			ImGui::End();
		}

		if (ImGui::Begin("PWindow", NULL, window_flags)) {
			ImGui::SetWindowSize({(float)WindowDims[0], 40});
			ImGui::SetWindowPos({0, (float)WindowDims[1] - (35)});
			for (unsigned int i = 0; i < P->numOfEntries; i++) {
				ImGuiDrawList = ImGui::GetWindowDrawList();
				if (i != 0)
					ImGui::SameLine();

				if (ImGui::ColorButton(PaletteIndex == i ? "Selected Color" : ("Color##" + std::to_string(i)).c_str(), {(float)P->entries[i][0]/255, (float)P->entries[i][1]/255, (float)P->entries[i][2]/255, (float)P->entries[i][3]/255}))
					PaletteIndex = i;

				if (PaletteIndex == i)
					ImGuiDrawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 0xFFFFFFFF, 0, 0, 1);
			};
			ImGui::End();
		}

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
		next_time += wait_time;
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
	FreeHistory();
	FreePalette(P);
	return 0;
}

void FrameBufferSizeCallback(GLFWwindow *window, int w, int h) {
	glViewport(0, 0, w, h);
}

void ZoomNLevelViewport() {
	// Simple hacky way to adjust canvas zoom level till it fits the window
	while (true) {
		if (ViewPort[2] >= WindowDims[1] || ViewPort[3] >= WindowDims[1]) {
			AdjustZoom(false);
			AdjustZoom(false);
			AdjustZoom(false);
			break;
		}
		AdjustZoom(true);
		ViewPort[2] = CanvasDims[0] * ZoomLevel;
		ViewPort[3] = CanvasDims[1] * ZoomLevel;
	}

	// Center On Screen
	ViewPort[0] = (float)WindowDims[0] / 2 - (float)CanvasDims[0] * ZoomLevel / 2;
	ViewPort[1] = (float)WindowDims[1] / 2 - (float)CanvasDims[1] * ZoomLevel / 2;
	ViewportSet();
}

void WindowSizeCallback(GLFWwindow* window, int width, int height) {
	WindowDims[0] = width;
	WindowDims[1] = height;

	// Center The Canvas On X, Y
	ViewPort[0] = (float)WindowDims[0] / 2 - (float)CanvasDims[0] * ZoomLevel / 2;
	ViewPort[1] = (float)WindowDims[1] / 2 - (float)CanvasDims[1] * ZoomLevel / 2;

	// Set The Canvas Size (Not Neccessary Here Tho)
	ViewPort[2] = CanvasDims[0] * ZoomLevel;
	ViewPort[3] = CanvasDims[1] * ZoomLevel;
	ViewportSet();
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		LMB_Pressed = action == GLFW_PRESS;

		int x = (int)(MousePosRel.X / ZoomLevel);
		int y = (int)(MousePosRel.Y / ZoomLevel);

		if (x >= 0 && x < CanvasDims[0] && y >= 0 && y < CanvasDims[1] && (Tool == BRUSH || Tool == ERASER || Tool == FILL || Tool == LINE || Tool == RECTANGLE)) {
			if (action == GLFW_PRESS) {
				MousePosRel.DownX = MousePosRel.X;
				MousePosRel.DownY = MousePosRel.Y;
				if (Tool == LINE || Tool == RECTANGLE) {
					SaveState();
				}
			}

			if (action == GLFW_RELEASE) {
				if (ImgDidChange == true) {
					SaveState();
					ImgDidChange = false;
				}
			}
		}
	}
}

void ProcessInput(GLFWwindow *window) {
	if (CanvasFreeze == 1) return;

	int x = (int)(MousePosRel.X / ZoomLevel);
	int y = (int)(MousePosRel.Y / ZoomLevel);

	if (!(x >= 0 && x < CanvasDims[0] && y >= 0 && y < CanvasDims[1]))
		return;

	if ((Tool == LINE || Tool == RECTANGLE) && LMB_Pressed == true) {
		Undo();
		int st_x  = (int)(MousePosRel.DownX / ZoomLevel);
		int st_y  = (int)(MousePosRel.DownY / ZoomLevel);
		int end_x = (int)(MousePosRel.X / ZoomLevel);
		int end_y = (int)(MousePosRel.Y / ZoomLevel);
		if (Tool == LINE) {
			drawLine(st_x, st_y, end_x, end_y);
		} else {
			drawRect(st_x, st_y, end_x, end_y);
		}
		SaveState();
	} else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_1) == GLFW_PRESS) {
		if (x >= 0 && x < CanvasDims[0] && y >= 0 && y < CanvasDims[1]) {
			switch (Tool) {
				case ERASER:
				case BRUSH: {
					ImgDidChange = true;
					draw(x, y);
					drawInBetween(x, y, (int)(MousePosRel.LastX / ZoomLevel), (int)(MousePosRel.LastY / ZoomLevel));
					break;
				}
				case FILL: {
					unsigned char *ptr = GetPixel(x, y);
					// Color Clicked On.
					unsigned char color[4] = {
						*(ptr + 0),
						*(ptr + 1),
						*(ptr + 2),
						*(ptr + 3)
					};
					fill(x, y, color);
					break;
				}
				case INK_DROPPER: {
					unsigned char *ptr = GetPixel(x, y);
					// Color Clicked On.
					unsigned char color[4] = {
						*(ptr + 0),
						*(ptr + 1),
						*(ptr + 2),
						*(ptr + 3)
					};

					// For loop starts from 1 because we don't need the first color i.e. 0,0,0,0 or transparent black
					for (unsigned int i = 0; i < P->numOfEntries; i++) {
						if (COLOR_EQUAL(P->entries[i], color) == 1) {
							LastPaletteIndex = PaletteIndex;
							PaletteIndex = i;
							break;
						}
					}
					break;
				}
				default: {
					break;
				}
			}
		}
	}
}

void ScrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
	if (yoffset > 0)
		AdjustZoom(true);
	else
		AdjustZoom(false);
}

void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods) {
	if (action == GLFW_RELEASE) {
		if (mods == GLFW_MOD_CONTROL)
			IsCtrlDown = 0;

		if (mods == GLFW_MOD_SHIFT)
			IsShiftDown = 0;

		if (key == GLFW_KEY_SPACE) {
			Tool = LastTool;
		}
	}

	if (action == GLFW_PRESS) {
		if (mods == GLFW_MOD_CONTROL) {
			IsCtrlDown = 1;

			// if IsCtrlDown key is pressed and + or - is pressed, adjust the zoom size
			if (key == GLFW_KEY_EQUAL) {
				AdjustZoom(true);
			} else if (key == GLFW_KEY_MINUS) {
				AdjustZoom(false);
			}
		} else if (mods == GLFW_MOD_SHIFT) {
			IsShiftDown = 1;
		} else {
			if (key == GLFW_KEY_EQUAL) {
				if (BrushSize < 255) {
					BrushSize++;
				}
			} else if (key == GLFW_KEY_MINUS) {
				if (BrushSize != 1) {
					BrushSize--;
				}
			}
		}

		switch (key) {
			// case GLFW_KEY_K:
			// 	if (PaletteIndex > 1) {
			// 		PaletteIndex--;
			// 	}
			// 	break;
			// case GLFW_KEY_L:
			// 	if (PaletteIndex < P->numOfEntries-1) {
			// 		PaletteIndex++;
			// 	}
			// 	break;
			case GLFW_KEY_1:
				if (P->numOfEntries >= 1) {
					PaletteIndex = IsShiftDown ? 9 : 1;
				}
				break;
			case GLFW_KEY_2:
				if (P->numOfEntries >= 2) {
					PaletteIndex = IsShiftDown ? 10 : 2;
				}
				break;
			case GLFW_KEY_3:
				if (P->numOfEntries >= 3) {
					PaletteIndex = IsShiftDown ? 11 : 3;
				}
				break;
			case GLFW_KEY_4:
				if (P->numOfEntries >= 4) {
					PaletteIndex = IsShiftDown ? 12 : 4;
				}
				break;
			case GLFW_KEY_5:
				if (P->numOfEntries >= 5) {
					PaletteIndex = IsShiftDown ? 13 : 5;
				}
				break;
			case GLFW_KEY_6:
				if (P->numOfEntries >= 6) {
					PaletteIndex = IsShiftDown ? 14 : 6;
				}
				break;
			case GLFW_KEY_7:
				if (P->numOfEntries >= 7) {
					PaletteIndex = IsShiftDown ? 15 : 7;
				}
				break;
			case GLFW_KEY_8:
				if (P->numOfEntries >= 8) {
					PaletteIndex = IsShiftDown ? 16 : 8;
				}
				break;
			case GLFW_KEY_F:
				Tool = FILL;
				break;
			case GLFW_KEY_B:
				Mode = IsShiftDown ? SQUARE : CIRCLE;
				Tool = BRUSH;
				break;
			case GLFW_KEY_E:
				Mode = IsShiftDown ? SQUARE : CIRCLE;
				Tool = ERASER;
				if (PaletteIndex != 0) {
					LastPaletteIndex = PaletteIndex;
					PaletteIndex = 0;
				}
				break;
			case GLFW_KEY_L:
				Mode = IsShiftDown ? SQUARE : CIRCLE;
				Tool = LINE;
				break;
			case GLFW_KEY_R:
				Mode = IsShiftDown ? SQUARE : CIRCLE;
				Tool = RECTANGLE;
				break;
			case GLFW_KEY_I:
				LastTool = Tool;
				Tool = INK_DROPPER;
				break;
			case GLFW_KEY_SPACE:
				LastTool = Tool;
				Tool = PAN;
				break;
			case GLFW_KEY_Z:
				if (IsCtrlDown == 1) {
					Undo();
				}
				break;
			case GLFW_KEY_Y:
				if (IsCtrlDown == 1) {
					Redo();
				}
				break;
			case GLFW_KEY_N:
				if (IsCtrlDown == 1) ShowNewCanvasWindow = 1;
				break;
			case GLFW_KEY_S:
				if (mods == GLFW_MOD_ALT) { // Show Prompt To Save if Alt + S pressed
					char *filePath = tinyfd_saveFileDialog("Save A File", NULL, NumOfFilterPatterns, FileFilterPatterns, "Image File (.png, .jpg, .jpeg)");
					if (filePath != NULL) {
						FilePath = FixFileExtension(std::string(filePath));
						SaveImageFromCanvas(FilePath);
						glfwSetWindowTitle(window, WINDOW_TITLE_CSTR); // Simple Hack To Get The File Name from the path and set it to the window title
						FreeHistory();
						SaveState();
					}
				} else if (IsCtrlDown == 1) { // Directly Save Don't Prompt
					FilePath = FixFileExtension(FilePath);
					SaveImageFromCanvas(FilePath);
					FreeHistory();
					SaveState();
				}
				break;
			case GLFW_KEY_O: {
				if (IsCtrlDown == 1) {
					char *filePath = tinyfd_openFileDialog("Open A File", NULL, NumOfFilterPatterns, FileFilterPatterns, "Image File (.png, .jpg, .jpeg)", 0);
					if (filePath != NULL) {
						FilePath = std::string(filePath);
						LoadImageToCanvas(FilePath.c_str(), CanvasDims, &CanvasData);
						glfwSetWindowTitle(window, WINDOW_TITLE_CSTR); // Simple Hack To Get The File Name from the path and set it to the window title
						FreeHistory();
						SaveState();
					}
				}
			}
			default:
				break;
		}
	}
}

void ViewportSet() {
	glViewport(ViewPort[0], ViewPort[1], ViewPort[2], ViewPort[3]);
}

void AdjustZoom(bool increase) {
	if (increase == true) {
		if (ZoomLevel < UINT_MAX) { // Max Value Of Unsigned int
			ZoomLevel++;
		}
	} else {
		if (ZoomLevel != 1) { // if zoom is 1 then don't decrease it further
			ZoomLevel--;
		}
	}

	// Comment Out To Not Center When Zooming
	ViewPort[0] = (float)WindowDims[0] / 2 - (float)CanvasDims[0] * ZoomLevel / 2;
	ViewPort[1] = (float)WindowDims[1] / 2 - (float)CanvasDims[1] * ZoomLevel / 2;

	ViewPort[2] = CanvasDims[0] * ZoomLevel;
	ViewPort[3] = CanvasDims[1] * ZoomLevel;

	ViewportSet();
	ZoomText = "Zoom: " + std::to_string(ZoomLevel) + "x";
}

unsigned char* GetPixel(int x, int y) {
	if (x < 0 || y < 0 || x > CanvasDims[0] || y > CanvasDims[1]) {
		return NULL;
	}
	return CanvasData + ((y * CanvasDims[0] + x) * 4);
}

/*

 x0, y0 ------------------ x1, y0
        |                |
        |                |
        |                |
        |                |
        |                |
 x0, y1 ------------------ x1, y1

*/

void drawRect(int x0, int y0, int x1, int y1) {
	drawLine(x0, y0, x1, y0);
	drawLine(x1, y0, x1, y1);
	drawLine(x1, y1, x0, y1);
	drawLine(x0, y1, x0, y0);
}

// Bresenham's line algorithm
void drawLine(int x0, int y0, int x1, int y1) {
	int dx =  abs (x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs (y1 - y0), sy = y0 < y1 ? 1 : -1; 
	int err = dx + dy, e2; /* error value e_xy */

	for (;;) {
		draw(x0, y0);
		if (x0 == x1 && y0 == y1) break;
		e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; } /* e_xy+e_x > 0 */
		if (e2 <= dx) { err += dx; y0 += sy; } /* e_xy+e_y < 0 */
	}
}

/*
	Function Takes 4 Argument First 2 Are starting x, y coordinates,
	and second 2 are ending x, y coordinates.
	And using a while loop it draws between the 2 given coordinates,
	hence no gap is left when mouse is being moved very fast
*/
void drawInBetween(int st_x, int st_y, int end_x, int end_y) {
	while (st_x != end_x || st_y != end_y) {
		if (st_x < end_x) {
			st_x++;
		}
		if (st_x > end_x) {
			st_x--;
		}
		if (st_y < end_y) {
			st_y++;
		}
		if (st_y > end_y) {
			st_y--;
		}

		for (int dirY = -BrushSize / 2; dirY < BrushSize / 2 + 1; dirY++) {
			for (int dirX = -BrushSize / 2; dirX < BrushSize / 2 + 1; dirX++) {
				if (st_x + dirX < 0 || st_x + dirX >= CanvasDims[0] || st_y + dirY < 0 || st_y + dirY > CanvasDims[1])
					continue;

				if (Mode == CIRCLE && dirX * dirX + dirY * dirY > BrushSize / 2 * BrushSize / 2)
					continue;

				unsigned char *ptr = GetPixel(st_x + dirX, st_y + dirY);
				if (ptr == NULL)
					continue;

				// Set Pixel Color
				if (Tool == ERASER) {
					*ptr = 0;
					*(ptr + 1) = 0;
					*(ptr + 2) = 0;
					*(ptr + 3) = 0;
				} else {
					*ptr = SelectedColor[0]; // Red
					*(ptr + 1) = SelectedColor[1]; // Green
					*(ptr + 2) = SelectedColor[2]; // Blue
					*(ptr + 3) = SelectedColor[3]; // Alpha
				}
			}
		}
	}
}

void draw(int st_x, int st_y) {
	// dirY = direction Y
	// dirX = direction X

	// Loops From -BrushSize/2 To BrushSize/2, ex: -6/2 to 6/2 -> -3 to 3
	for (int dirY = -BrushSize / 2; dirY < BrushSize / 2 + 1; dirY++) {
		for (int dirX = -BrushSize / 2; dirX < BrushSize / 2 + 1; dirX++) {
			if (st_x + dirX < 0 || st_x + dirX >= CanvasDims[0] || st_y + dirY < 0 || st_y + dirY > CanvasDims[1])
				continue;

			if (Mode == CIRCLE && dirX * dirX + dirY * dirY > BrushSize / 2 * BrushSize / 2)
				continue;

			unsigned char* ptr = GetPixel(
				CLAMP_INT(st_x + dirX, 0, CanvasDims[0] - 1),
				CLAMP_INT(st_y + dirY, 0, CanvasDims[1] - 1)
			);

			// Set Pixel Color
			if (Tool == ERASER) {
				*ptr = 0;
				*(ptr + 1) = 0;
				*(ptr + 2) = 0;
				*(ptr + 3) = 0;
			} else {
				*ptr = SelectedColor[0]; // Red
				*(ptr + 1) = SelectedColor[1]; // Green
				*(ptr + 2) = SelectedColor[2]; // Blue
				*(ptr + 3) = SelectedColor[3]; // Alpha
			}
		}
	}
}

// Fill Tool, Fills The Whole Canvas Using Recursion
void fill(int x, int y, unsigned char *old_color) {
	unsigned char *ptr = GetPixel(x, y);
	if (COLOR_EQUAL(ptr, old_color)) {
		ImgDidChange = true;
		*ptr = SelectedColor[0];
		*(ptr + 1) = SelectedColor[1];
		*(ptr + 2) = SelectedColor[2];
		*(ptr + 3) = SelectedColor[3];

		if (x != 0 && !COLOR_EQUAL(GetPixel(x - 1, y), SelectedColor))
			fill(x - 1, y, old_color);
		if (x != CanvasDims[0] - 1 && !COLOR_EQUAL(GetPixel(x + 1, y), SelectedColor))
			fill(x + 1, y, old_color);
		if (y != CanvasDims[1] - 1 && !COLOR_EQUAL(GetPixel(x, y + 1), SelectedColor))
			fill(x, y + 1, old_color);
		if (y != 0 && !COLOR_EQUAL(GetPixel(x, y - 1), SelectedColor))
			fill(x, y - 1, old_color);
	}
}

// Makes sure that the file extension is .png or .jpg/.jpeg
std::string FixFileExtension(std::string filepath) {
	std::string fileExt = filepath.substr(filepath.find_last_of(".") + 1);
	std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), [](unsigned char c){ return std::tolower(c); });

	if (fileExt != "png" && fileExt != "jpg" && fileExt != "jpeg") {
		filepath = filepath + ".png";
	}

	return filepath;
}

void SaveImageFromCanvas(std::string filepath) {
	std::string fileExt = filepath.substr(filepath.find_last_of(".") + 1);
	// Convert File Extension to LowerCase
	std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), [](unsigned char c){ return std::tolower(c); });

	if (fileExt == "png") {
		WritePngFromCanvas(filepath.c_str(), CanvasDims);
	} else if (fileExt == "jpg" || fileExt == "jpeg") {
		WriteJpgFromCanvas(filepath.c_str(), CanvasDims);
	} else {
		filepath = filepath + ".png";
		WritePngFromCanvas(filepath.c_str(), CanvasDims);
	}
	ShouldSave = 0;
}

/*
	Pushes Pixels On Current Canvas in "History" array at index "HistoryIndex"
	Removes The Elements in a range from "History" if "IsDirty" is true
*/
void SaveState() {
	// Runs When We Did Undo And Tried To Modify The Canvas
	if (CurrentState != NULL && CurrentState->next != NULL) {
		cvstate_t* tmp;
		cvstate_t* head = CurrentState->next; // we start freeing from the next node of current node

		while (head != NULL) {
			tmp = head;
			head = head->next;
			if (tmp->pixels != NULL) {
				free(tmp->pixels);
			}
			free(tmp);
		}
	}

	cvstate_t* NewState = (cvstate_t*) malloc(sizeof(cvstate_t));
	NewState->pixels = (unsigned char*) malloc(CANVAS_SIZE_B);

	if (CurrentState == NULL) {
		CurrentState = NewState;
		CurrentState->prev = NULL;
		CurrentState->next = NULL;
	} else {
		NewState->prev = CurrentState;
		NewState->next = NULL;
		CurrentState->next = NewState;
		CurrentState = NewState;
	}

	memset(CurrentState->pixels, 0, CANVAS_SIZE_B);
	memcpy(CurrentState->pixels, CanvasData, CANVAS_SIZE_B);
}

// Undo - Puts The Pixels from "History" at "HistoryIndex"
int Undo() {
	if (CurrentState->prev != NULL) {
		CurrentState = CurrentState->prev;
		memcpy(CanvasData, CurrentState->pixels, CANVAS_SIZE_B);
	}
	return 0;
}

// Redo - Puts The Pixels from "History" at "HistoryIndex"
int Redo() {
	if (CurrentState->next != NULL) {
		CurrentState = CurrentState->next;
		memcpy(CanvasData, CurrentState->pixels, CANVAS_SIZE_B);
	}

	return 0;
}

/*
	Function: FreeHistory()
	Takes The CurrentState Node
		- Frees All Of The Nodes Before It
		- Frees All Of The Nodes After It
*/
void FreeHistory() {
	if (CurrentState == NULL) return;

	cvstate_t* tmp;
	cvstate_t* head = CurrentState->prev;

	while (head != NULL) {
		tmp = head;
		head = head->prev;
		if (tmp != NULL && tmp->pixels != NULL) {
			free(tmp->pixels);
			tmp->pixels = NULL;
			free(tmp);
			tmp = NULL;
		}
	}

	head = CurrentState;

	while (head != NULL) {
		tmp = head;
		head = head->next;
		if (tmp != NULL && tmp->pixels != NULL) {
			free(tmp->pixels);
			tmp->pixels = NULL;
			free(tmp);
			tmp = NULL;
		}
	}

	CurrentState = NULL;
}
