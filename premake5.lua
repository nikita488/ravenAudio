workspace "ravenAudio"
	kind "ConsoleApp"
	language "C++"
	system "Windows"
	systemversion "latest"
	location "build"
	configurations { "Debug", "Release" }
	targetdir "bin/%{cfg.buildcfg}"
	
	filter "configurations:Debug"
		defines { "WIN32", "_DEBUG", "_CONSOLE" }
		symbols "On"
	filter {}
	
	filter "configurations:Release"
		defines { "WIN32", "NDEBUG", "_CONSOLE" }
		optimize "On"
	filter {}
   
	files { "src/*.*" }
   
project "ravenAudio"