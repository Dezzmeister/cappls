# CMakeList.txt : CMake project for cappls, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("cappls")

# Add source to this project's executable.
add_executable (cappls "lib.h" "lib.c" "venc.h" "main.c" "com.h" "com.c" "venc.c" "async_callbacks.h" "async_callbacks.c" "args.h" "args.c" "input.h" "input.c")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET cappls PROPERTY C_STANDARD 23)
endif()

if (MSVC)
	target_compile_options(cappls PRIVATE
		/Os
		/GS-
		/Zl
		/WX
	)
	target_link_options(cappls PRIVATE
		/SUBSYSTEM:CONSOLE
		/NODEFAULTLIB
		/LTCG
		/OPT:REF
		/OPT:ICF
		/ENTRY:entry
	)
endif()

target_link_libraries(cappls PRIVATE
	Ole32.lib
	D3D11.lib
	dxguid.lib
	dxgi.lib
	Mfplat.lib
	Mfreadwrite.lib
	Mfuuid.lib
	Propsys.lib
	Mf.lib
)

string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
string(REPLACE "/RTC" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
