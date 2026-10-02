#ifndef PIKMIN_PC_OPENGL_H
#define PIKMIN_PC_OPENGL_H

// Punto único de entrada a las cabeceras de OpenGL.
//
// En Windows, <GL/gl.h> y <GL/glext.h> necesitan las convenciones de llamada
// APIENTRY, WINGDIAPI y CALLBACK, que normalmente obtienen incluyendo
// <windows.h>. Aquí no podemos permitirlo: windows.h define macros con nombres
// muy comunes, y una de ellas es ERROR, que este proyecto usa como macro de
// registro propia (include/DebugLog.h) en 371 archivos. Incluir windows.h en la
// ruta gráfica la aplastaría.
//
// Tanto glext.h como gl.h condicionan su #include <windows.h> a que APIENTRY no
// esté ya definida, así que definiéndola nosotros primero se saltan windows.h
// por completo y obtenemos solo OpenGL. Las definiciones son las mismas que
// windows.h habría proporcionado.
//
// El juego carga las funciones modernas de GL con SDL_GL_GetProcAddress, así
// que no hace falta ni GLAD ni GLEW en ningún sistema.

#ifdef _WIN32
#  ifndef APIENTRY
#    define APIENTRY __stdcall
#  endif
#  ifndef WINGDIAPI
#    define WINGDIAPI __declspec(dllimport)
#  endif
#  ifndef CALLBACK
#    define CALLBACK __stdcall
#  endif
#endif

// Fuera de la build de Android PIKI_USE_GLES no está definido; darle valor
// evita que las expresiones `PIKI_USE_GLES == 0` fallen.
#ifndef PIKI_USE_GLES
#  define PIKI_USE_GLES 0
#endif
#if PIKI_USE_GLES
#  include <GLES3/gl3.h>
#  include <GLES3/gl3ext.h>
// Timer queries: en GLES viven en EXT_disjoint_timer_query (gl2ext.h) con
// sufijo EXT; el mismo nombre que en escritorio deja el resto del código
// igual. Si el driver no las expone, los punteros quedan nulos y se ignoran.
#  include <GLES2/gl2ext.h>
#  ifndef GL_TIME_ELAPSED
#    define GL_TIME_ELAPSED GL_TIME_ELAPSED_EXT
#  endif
#  ifndef APIENTRYP
#    define APIENTRYP GL_APIENTRYP
#  endif
#elif defined(__APPLE__)
// macOS solo ofrece un contexto 2.1 heredado o uno Core 4.1 (sin perfil de
// compatibilidad); pc_window.cpp decide cuál. <OpenGL/gl.h> aporta los tipos y
// las funciones 1.x; las cabeceras de Apple no traen los typedef PFNGL*PROC,
// así que el glext.h de Khronos que distribuye SDL ocupa el lugar de glext.h.
// Todo lo posterior a GL 1.1 se carga igualmente con SDL_GL_GetProcAddress.
#  ifndef GL_SILENCE_DEPRECATION
#    define GL_SILENCE_DEPRECATION
#  endif
#  define GL_GLEXT_LEGACY
#  include <OpenGL/gl.h>
// gl.h de Apple marca 1.2-2.1 como presentes sin declarar sus PFN; retirar las
// marcas deja que glext.h emita esos typedef (las constantes coinciden).
#  undef GL_VERSION_1_2
#  undef GL_VERSION_1_3
#  undef GL_VERSION_1_4
#  undef GL_VERSION_1_5
#  undef GL_VERSION_2_0
#  undef GL_VERSION_2_1
#  include <SDL2/SDL_opengl_glext.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif

// Red de seguridad: si alguna cabecera del sistema acabara arrastrando
// windows.h de todos modos, estas macros suyas chocan con identificadores del
// juego. Retirarlas aquí hace que un conflicto se manifieste como un error de
// compilación claro en vez de como comportamiento extraño.
#ifdef _WIN32
#  undef near
#  undef far
#  undef small
#endif

#endif // PIKMIN_PC_OPENGL_H
