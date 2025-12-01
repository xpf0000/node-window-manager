{
  "targets": [
    {
      "target_name": "addon",
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "conditions":[
        ["OS=='win'", {
      	  "sources": [
      	    "lib/win_d3d_helpers.h",
            "lib/win_capture_interop.h",
            "lib/win_capture_manager.h",
            "lib/win_capture_manager.cc",
            "lib/windows.cc"
      	  ],
          "libraries": [
            "Dwmapi.lib",
            "d3d11.lib",
            "dxgi.lib",
            "windowscodecs.lib",
            "shcore.lib"
          ],
            "defines": [
              "WIN32_LEAN_AND_MEAN",
              "NOMINMAX",
              "UNICODE",
              "_UNICODE"
            ],
            "msvs_settings": {
               "VCCLCompilerTool": {
                  "ExceptionHandling": 1,
                  "AdditionalOptions": [
                     "/std:c++20",
                     "/EHsc"
                  ]
               }
            }
      	}],
        ["OS=='mac'", {
      	  "sources": [ "lib/macos.mm" ],
          "libraries": [ '-framework AppKit', '-framework ApplicationServices' ],
          "xcode_settings": {
                      "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
                      "CLANG_CXX_LIBRARY": "libc++"
                    }
      	}],
        ["OS=='linux'", {
          "sources": [ "lib/linux.cpp" ]
        }]
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS', "NODE_ADDON_API_ENABLE_MAYBE_SURFACE" ],
    }
  ]
}
