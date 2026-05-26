idf_component_register(
    SRCS
        "Camera.cpp"
        "Light.cpp"
        "Material.cpp"
        "Object.cpp"
        "PostFX.cpp"
        "Primitives.cpp"
        "Renderer.cpp"
        "Scene.cpp"
        "Texture.cpp"
        "TrigLUT.cpp"
    INCLUDE_DIRS "."
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
