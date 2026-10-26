cmake_minimum_required(VERSION 3.24) # OVERRIDE_FIND_PACKAGE (FetchContent SDL3)
project(eliott-visu VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
)

target_link_libraries(imgui PUBLIC glfw)
add_library(imgui::imgui ALIAS imgui)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG master
)
FetchContent_MakeAvailable(imguizmo)

target_link_libraries(imguizmo PUBLIC imgui)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    OVERRIDE_FIND_PACKAGE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

find_package(OpenGL REQUIRED)

add_library(glad STATIC
    extern/glad/src/glad.c
)

target_include_directories(glad PUBLIC extern/glad/include)
target_link_libraries(glad PUBLIC OpenGL::GL)
add_library(glad::glad ALIAS glad)

add_subdirectory(extern/eliott-engine-3d "${CMAKE_BINARY_DIR}/ee-core")

FetchContent_Declare(
    tinyfiledialogs
    GIT_REPOSITORY https://github.com/native-toolkit/tinyfiledialogs.git
    GIT_TAG master
)
FetchContent_MakeAvailable(tinyfiledialogs)

add_library(tinyfiledialogs STATIC
    ${tinyfiledialogs_SOURCE_DIR}/tinyfiledialogs.c
)

target_include_directories(tinyfiledialogs PUBLIC
    ${tinyfiledialogs_SOURCE_DIR}
)

if(WIN32)
    target_link_libraries(tinyfiledialogs PUBLIC comdlg32 ole32)
endif()

# Sous Linux, tinyfiledialogs retombe sur un dialogue dans le terminal quand
# zenity/kdialog sont absents : on passe par le portail XDG (selecteur natif).
if(UNIX AND NOT APPLE)
    FetchContent_Declare(
        nfd
        GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
        GIT_TAG v1.2.1
    )
    set(NFD_PORTAL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nfd)
endif()


add_executable(eliott-visu
    src/main.cpp
    src/App.cpp
    src/core/Project.cpp
    src/core/GameProcess.cpp
    src/core/SceneHistory.cpp
    src/ui/FolderTree.cpp
    src/ui/SystemPanel.cpp
    src/ui/CodeDock.cpp
    src/ui/ComponentPanel.cpp
    src/ui/MainPanel.cpp
    src/ui/ShowCodePanel.cpp
    src/ui/FileReader.cpp
    src/helpers/OpenExternal.cpp
    src/helpers/SystemGen.cpp
    src/ui/Modal.cpp
    src/ui/ConfirmModal.cpp
    src/ui/CreateSystemModal.cpp
    src/ui/CreateComponentModal.cpp
    src/ui/CreateSceneModal.cpp
    src/helpers/ComponentGetter.cpp
    src/helpers/ComponentGen.cpp
    src/helpers/SceneGen.cpp
    src/helpers/GenerateCatalog.cpp
    src/helpers/BuildProject.cpp
    src/ui/ScenePreview.cpp
    extern/ImGuiColorTextEdit/TextEditor.cpp
    extern/ImGuiColorTextEdit/LanguageDefinitions.cpp
)

target_include_directories(eliott-visu PUBLIC
    include/
    extern/ImGuiColorTextEdit/
    extern/fonts/
)

# Catalogue des composants standard integre au binaire : l'editeur n'a besoin
# d'aucun fichier a cote de lui (le binaire est telecharge seul par eliott-hub).
file(READ "${CMAKE_SOURCE_DIR}/assets/Components.json" STANDARD_COMPONENTS_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/Components.json")
configure_file(
    cmake/StandardComponents.hpp.in
    "${CMAKE_BINARY_DIR}/generated/visu/helpers/StandardComponents.hpp"
    @ONLY
)

# Liste des commandes (Ctrl+K) : relue depuis assets/Commands.json a chaque
# ouverture (modifiable sans recompiler) ; copie integree si le fichier manque.
set(EDITOR_COMMANDS_PATH "${CMAKE_SOURCE_DIR}/assets/Commands.json")
file(READ "${EDITOR_COMMANDS_PATH}" EDITOR_COMMANDS_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${EDITOR_COMMANDS_PATH}")
configure_file(
    cmake/EditorCommands.hpp.in
    "${CMAKE_BINARY_DIR}/generated/visu/ui/EditorCommands.hpp"
    @ONLY
)
target_include_directories(eliott-visu PRIVATE "${CMAKE_BINARY_DIR}/generated")

target_link_libraries(eliott-visu PRIVATE
    ee-core          # coeur moteur : amene aussi glad, glfw, OpenGL, nlohmann_json, eliott-ecs, eliott-input
    imgui::imgui
    imguizmo::imguizmo
    tinyfiledialogs
)

if(UNIX AND NOT APPLE)
    target_link_libraries(eliott-visu PRIVATE nfd)
endif()

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(eliott-visu PRIVATE -static)
endif()
