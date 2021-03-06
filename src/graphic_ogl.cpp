#include <renik\cpp\graphic.h>
#include <renik\cpp\window.h>

#ifdef RENIK_GL
#include <GLEW\glew.h>
#include <GLEW\wglew.h>
#include "utility.h"

namespace renik {
	namespace Graphic {
		namespace __internal__ {
			bool IGraphic_OGL::checkShaderCompilation(void* handler,uint kind) {
				int res;
				int InfoLogLength;
				glGetShaderiv((uint)handler, GL_COMPILE_STATUS, &res);
				glGetShaderiv((uint)handler, GL_INFO_LOG_LENGTH, &InfoLogLength);
				if (InfoLogLength > 0) {
					std::vector<char> ErrorMessage(InfoLogLength + 1);
					glGetShaderInfoLog((uint)handler, InfoLogLength, NULL, &ErrorMessage[0]);
					if(logCallback != nullptr)
						logCallback(-1, this->get_GraphicType(), Util::StringFormat("Failed to Compile %s Shader: \n%s", kind == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT", &ErrorMessage[0]));
					return false;
				}
				return true;
			}
			bool IGraphic_OGL::checkShaderProgram(void* handler, uint kind) {
				GLint res;
				glGetProgramiv((uint)handler, kind, &res);
				if (res != GL_TRUE) {
					int log_length = 1024;
					GLchar message[1024];
					glGetProgramInfoLog((uint)handler, 1024, &log_length, message);
					if (logCallback != nullptr)
						logCallback(-1,this->get_GraphicType(), Util::StringFormat("Failed to %s Shader: \n%s", kind == GL_LINK_STATUS ? "LINK" : "VALIDATE", message));
					glDeleteProgram((uint)handler);
					return false;
				}
				return true;
			}
			
			inline GraphicShaderInputDataType __getShaderInputDataType(uint code) {
				switch (code) {
				case GL_FLOAT:			return GraphicShaderInputDataType::FLOAT;
				case GL_FLOAT_VEC2:		return GraphicShaderInputDataType::VEC2;
				case GL_FLOAT_VEC3:		return GraphicShaderInputDataType::VEC3;
				case GL_FLOAT_VEC4:		return GraphicShaderInputDataType::VEC4;
				case GL_FLOAT_MAT2:		return GraphicShaderInputDataType::MAT2X2;
				case GL_FLOAT_MAT3:		return GraphicShaderInputDataType::MAT3X3;
				case GL_FLOAT_MAT4:		return GraphicShaderInputDataType::MAT4X4;
				}
				return GraphicShaderInputDataType::UNKNOWN;
			}
			inline uint __getDrawMode(GraphicDrawMode code) {
				switch (code) {
					case GraphicDrawMode::POINTS :			return GL_POINTS;
					case GraphicDrawMode::LINE_STRIP:		return GL_LINE_STRIP;
					case GraphicDrawMode::LINE_LOOP:		return GL_LINE_LOOP;
					case GraphicDrawMode::LINES:			return GL_LINES;
					case GraphicDrawMode::TRIANGLE_STRIP:	return GL_TRIANGLE_STRIP;
					case GraphicDrawMode::TRIANGLE_FAN:		return GL_TRIANGLE_FAN;
					case GraphicDrawMode::TRIANGLES:		return GL_TRIANGLES;
				}
				return GL_FALSE;
			}
			inline void __getErr(gLogCallback callBack, const char* funName, uint line) {
				uint code = glGetError();
				if (code != GL_NO_ERROR) {
					if (callBack != nullptr) {
						callBack(code, (int)GraphicBackend::OPENGL, Util::StringFormat("ERROR:%i in %s line %i", code, funName,line-1));
					}
				}
			}

			IGraphic_OGL::IGraphic_OGL(Surface* surface) : IGraphic::IGraphic(surface) { 
				if (surface != nullptr)
					this->_surface = surface;
				_libShader = std::vector<Shader>();
				_handler = std::unordered_map<const char*, void*>();
				_meshHandler = std::unordered_map<Mesh*, _MeshHandler>();
			}
			IGraphic_OGL::~IGraphic_OGL() {
				_libShader.clear();
				_handler.clear();
				_gctx = nullptr;
				_surface = nullptr;
			}

			int IGraphic_OGL::get_GraphicType() { return (int)GraphicBackend::OPENGL; }
			bool IGraphic_OGL::get_IsInitialized() { return IGraphic::get_IsInitialized(); }
			Surface* IGraphic_OGL::get_Surface() { return IGraphic::get_Surface(); }
			std::vector<Shader> IGraphic_OGL::get_Shaders() { return IGraphic::get_Shaders(); }
			Shader* IGraphic_OGL::get_Shader(id_t shaderID) { return IGraphic::get_Shader(shaderID); }

			int IGraphic_OGL::Init() {
				_initialized = false;
				PIXELFORMATDESCRIPTOR pDesc = {
					sizeof(PIXELFORMATDESCRIPTOR),
					1,
					PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
					PFD_TYPE_RGBA,	      // The kind of framebuffer. RGBA or palette.
					32,					  // Colordepth of the framebuffer.
					0, 0, 0, 0, 0, 0,
					0,
					0,
					0,
					0, 0, 0, 0,
					24,                   // Number of bits for the depthbuffer
					8,                    // Number of bits for the stencilbuffer
					0,                    // Number of Aux buffers in the framebuffer. (NOT SUPPORTED,,,, WTF?)
					0,				      // Not Supported
					0,					  // Not Supported
					0, 0, 0				  // Not Supported
				};
				Window::WinHandle* winSurface = (Window::WinHandle*)_surface->winData;

				int pixFmt = ChoosePixelFormat((HDC)winSurface->hwndDC, &pDesc);
				if (!pixFmt) {
					if (logCallback)
						logCallback(-1, this->get_GraphicType(), Util::StringFormat("Failed to Choose Pixel Format: %S", Util::LastSysErr()));
					return false;
				}
				SetPixelFormat((HDC)winSurface->hwndDC, pixFmt, &pDesc);
				this->_gctx = wglCreateContext((HDC)winSurface->hwndDC);
				if (!this->_gctx) {
					if (logCallback)
						logCallback(-1, this->get_GraphicType(), Util::StringFormat("Failed to create Context: %S", Util::LastSysErr()));
					return false;
				}
				wglMakeCurrent((HDC)winSurface->hwndDC, (HGLRC)this->_gctx);

				if (glewInit() != GLEW_OK) {
					if (logCallback)
						logCallback(-1, this->get_GraphicType(), Util::StringFormat("Failed to initializing GLEW: %S", Util::LastSysErr()));
					return false;
				}

				//Initializing Some Handler
				int fb;
				glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
				uint vao;
				glGenVertexArrays(1, &vao);
				_handler["fb"] = (void*)(uint)fb;
				_handler["vao"] = (void*)vao;

				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

				_surface->gInterface = this;
				_initialized = true;
				_surface->gInterface = this;

				_initialized = true;
				return true;
			}
			int IGraphic_OGL::Release() { 
				_initialized = false;
				_surface->gInterface = nullptr;
				return true;
			}
			
			bool IGraphic_OGL::CheckFeature(int feature) { 
				return glIsEnabled((GLenum)feature);
			}
			int IGraphic_OGL::EnableFeature(int feature, void(*fAction)(void*)) {
				glEnable((GLenum)feature);
				if (fAction != nullptr)
					fAction(this->_gctx);
				return true;
			}
			int IGraphic_OGL::DisableFeature(int feature, void(*fAction)(void*)) {
				glDisable((GLenum)feature);
				if (fAction != nullptr)
					fAction(this->_gctx);
				return true;
			}
			
			id_t IGraphic_OGL::CreateFrameBuffer() {
				return 0U;
			}
			int IGraphic_OGL::DeleteFrameBuffer(id_t handler) {
				return 0;
			}
			int IGraphic_OGL::BindFrameBuffer(id_t handler) {
				return 0;
			}
			
			id_t IGraphic_OGL::CreateRenderBuffer() {
				return 0U;
			}
			int IGraphic_OGL::DeleteRenderBuffer(id_t handler) {
				return 0;
			}
			int IGraphic_OGL::BindRenderBuffer(id_t handler) {
				return 0;
			}
			
			int IGraphic_OGL::BindMesh(Mesh* mesh, bool rebind) {
				if (mesh == nullptr)
					return false;
				glBindBuffer(GL_ARRAY_BUFFER, NULL);
				__getErr(this->logCallback, "glBindBuffer", __LINE__);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, NULL);
				__getErr(this->logCallback, "glBindBuffer", __LINE__);
				//Check mesh
				if (!rebind) {
					for (auto m : _meshHandler) {
						if (m.first == mesh) {
							glBindBuffer(GL_ARRAY_BUFFER, (uint)m.second.vbo);
							__getErr(this->logCallback, "glBindBuffer", __LINE__);
							glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (uint)m.second.ibo);
							__getErr(this->logCallback, "glBindBuffer", __LINE__);
							return true;
						}
					}
				}
				uint hPtr[2];
				glGenBuffers(2, hPtr);
				glBindBuffer(GL_ARRAY_BUFFER, hPtr[0]);
				size_t vLen = mesh->get_vertexSize();
				glBufferData(GL_ARRAY_BUFFER, vLen, NULL, mesh->isStatic ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
				__getErr(this->logCallback, "glBufferData", __LINE__);
				size_t offset = 0U;
				auto ptrs = mesh->get_vertexPtrs();
				for (auto v : ptrs) {
					auto size = v.size;
					glBufferSubData(GL_ARRAY_BUFFER, offset, size, v.ptr);
					__getErr(this->logCallback, "glBufferSubData", __LINE__);
					offset += size;
				}
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hPtr[1]);

				size_t iLen = mesh->get_indexSize() * sizeof(float);
				auto iPtr = mesh->get_indexPtr();
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, iLen, iPtr.ptr, mesh->isStatic ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
				__getErr(this->logCallback, "glBufferSubData", __LINE__);

				_MeshHandler handler = { (void*)hPtr[0], (void*)hPtr[1] };
				_meshHandler[mesh] = handler;
				return true;
			}
			int IGraphic_OGL::UnbindMesh(Mesh* mesh, bool release) { 
				return false;
			}
			
			id_t IGraphic_OGL::BindTexture(Texture* tex) {
				return false;
			}
			int IGraphic_OGL::UnbindTexture(id_t handler) {
				return false;
			}
			
			Shader* IGraphic_OGL::CreateShader(const char* vertexShaderSrc, const char* fragmentShaderSrc, const char* name) { 
				if (vertexShaderSrc == nullptr || fragmentShaderSrc == nullptr) {
					return false;
				}
				//Compile Vertex Shader
				auto vHandle = glCreateShader(GL_VERTEX_SHADER);
				glShaderSource(vHandle, 1, &vertexShaderSrc, NULL);
				glCompileShader(vHandle);
				if (!checkShaderCompilation((void*)vHandle, GL_VERTEX_SHADER))
					return false;

				//Compile Fragment Shader
				auto fHandle = glCreateShader(GL_FRAGMENT_SHADER);
				glShaderSource(fHandle, 1, &fragmentShaderSrc, NULL);
				glCompileShader(fHandle);
				if (!checkShaderCompilation((void*)fHandle, GL_FRAGMENT_SHADER))
					return false;

				//Linking Program
				auto shaderHandler = glCreateProgram();
				glAttachShader(shaderHandler, vHandle);
				glAttachShader(shaderHandler, fHandle);
				glLinkProgram(shaderHandler);
				if (!checkShaderProgram((void*)shaderHandler, GL_LINK_STATUS))
					return false;

				//Validating Program
				glValidateProgram(shaderHandler);
				if (!checkShaderProgram((void*)shaderHandler, GL_VALIDATE_STATUS))
					return false;

				glDetachShader(shaderHandler, vHandle);
				glDetachShader(shaderHandler, fHandle);

				glDeleteShader(vHandle);
				glDeleteShader(fHandle);

				Shader newShader = Shader();
				newShader.name = name;
				newShader.handle = (void*)shaderHandler;
				newShader.input = std::unordered_map <std::string, GraphicShaderInputInfo > ();

				int maxlength = 0;
				int count = 0;

				//Get Exposed Attributes
				glGetProgramiv(shaderHandler, GL_ACTIVE_ATTRIBUTES, &count);
				glGetProgramiv(shaderHandler, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &maxlength);
				std::vector<char> buffer(maxlength);
				for (int i = count - 1; i > -1; i--) {
					int size = 0;
					int len = 0;
					uint type = 0;
					glGetActiveAttrib(shaderHandler, i, buffer.size(), &len, &size, &type, &buffer[0]);
					std::string name((char*)&buffer[0], len);
					GraphicShaderInputInfo info = {};
					info.type = GraphicShaderInputType::ATTRIBUTE;
					info.dataType = __getShaderInputDataType(type);
					newShader.input[name] = info;
				}

				//Get Exposed Uniform
				glGetProgramiv(shaderHandler, GL_ACTIVE_UNIFORMS, &count);
				buffer = std::vector<char>(256);
				for (int i = count - 1; i > -1; i--) {
					int size = 0;
					int len = 0;
					uint type = 0;
					glGetActiveUniform(shaderHandler, i, 256, &len, &size, &type, &buffer[0]);
					std::string name((char*)&buffer[0], len - 1);
					GraphicShaderInputInfo info = {};
					info.type = GraphicShaderInputType::UNIFORM;
					info.dataType = __getShaderInputDataType(type);
					newShader.input[name] = info;
				}

				_libShader.push_back(newShader);
				return &_libShader[_libShader.size() - 1];

				return nullptr;
			}
			int IGraphic_OGL::AttachShaderToMaterial(Shader* shader, Material* material){ 
				if (shader == nullptr || material == nullptr)
					return false;
				for (auto sP : shader->input) {
					uint loc = 0U;
					if (sP.second.type == GraphicShaderInputType::ATTRIBUTE)
						loc = (uint)glGetAttribLocation((uint)shader->handle, sP.first.c_str());
					else
						loc = (uint)glGetUniformLocation((uint)shader->handle, sP.first.c_str());
					material->shaderInputHandler[sP.first] = (void*)loc;
				}
				material->shader = shader;
				return true;
			}
			int IGraphic_OGL::DestroyShader(Shader* shader){
				if (shader == nullptr || shader->handle == nullptr)
					return false;
				return false;
			}
		
			int IGraphic_OGL::DrawMesh(Mesh* mesh, GraphicDrawMode drawMode) {
				glUseProgram((uint)mesh->material->shader->handle);
				__getErr(this->logCallback, "glUseProgram", __LINE__);
				BindMesh(mesh);
				size_t offset = 0U;
				for (auto v : mesh->material->shaderInputHandler) {
					auto meshPtr = mesh->get_vertexPtr(v.first.c_str());
					glEnableVertexAttribArray((uint)v.second);
					__getErr(this->logCallback, "glEnableVertexAttribArray", __LINE__);
					glVertexAttribPointer((uint)v.second, meshPtr.stride, GL_FLOAT, GL_FALSE, 0, (void*)offset);
					__getErr(this->logCallback, "glVertexAttribPointer", __LINE__);
					offset += meshPtr.size;
				}
				glDrawElements(__getDrawMode(drawMode), mesh->get_indexSize(), GL_UNSIGNED_INT, (void*)NULL);
				for (auto m : mesh->material->shaderInputHandler) {
					glDisableVertexAttribArray((uint)m.second);
					__getErr(this->logCallback, "glDisableVertexAttribArray", __LINE__);
				}
				glBindBuffer(GL_ARRAY_BUFFER, NULL);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, NULL);
				glUseProgram(0);
				return false;
			} 
			int IGraphic_OGL::DrawVertices(Memory::MappedBuffer<float>* vertices, GraphicDrawMode mode) {
				return false;
			}
			int IGraphic_OGL::DrawViewPort(RectI rect) {
				//OpenGL always start from bottom-left screen, so we need to flip that
				glViewport(rect.x, rect.height - rect.y, rect.width, rect.height);
				return true;
			}
			int IGraphic_OGL::DrawScissor(RectI rect) {
				//OpenGL always start from bottom-left screen, so we need to flip that
				glScissor(rect.x, rect.height - rect.y, rect.width, rect.height);
				return false;
			}
			
			int IGraphic_OGL::ClearColor(const Color& color) {
				glClearColor(color.r, color.g, color.b, color.a);
				return false;
			} 
			int IGraphic_OGL::ClearDepth(const float& depth) { 
				glClearDepthf(depth);
				return false;
			} 
			int IGraphic_OGL::ClearStencil(const int& stencil) { 
				glClearStencil(stencil);
				return false;
			} 
			
			int IGraphic_OGL::BeginRender() { 
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				return true;
			} 
			int IGraphic_OGL::EndRender() {
				SwapBuffers((HDC)((Window::WinHandle*)_surface->winData)->hwndDC);
				return true;
			} 
		}
	}
}
#endif