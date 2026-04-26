from setuptools import Extension, setup


class GetPybindInclude:
    def __str__(self) -> str:
        import pybind11

        return pybind11.get_include()


ext_modules = [
    Extension(
        "aerokey._aerokey",
        ["bindings/pybind_module.cpp", "src/aero_key.cpp"],
        include_dirs=["include", str(GetPybindInclude())],
        language="c++",
        extra_compile_args=["-std=c++17"],
    )
]

setup(ext_modules=ext_modules)
