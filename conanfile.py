import os
import re
import tarfile

from tempfile import TemporaryDirectory

from conan import ConanFile
from conan.errors import ConanException
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.build import can_run
from conan.tools.files import copy,load

class audio(ConanFile):
    name = "viam-audio"

    license = "Apache-2.0"
    url = "https://github.com/viam-modules/system-audio"
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"

    options = {
        "shared": [True, False]
    }
    default_options = {
        "shared": True
    }

    exports_sources = "CMakeLists.txt", "LICENSE", "src/*", "test/*", "meta.json", "run.sh"

    def set_version(self):
        content = load(self, "CMakeLists.txt")
        self.version = re.search("set\(CMAKE_PROJECT_VERSION (.+)\)", content).group(1).strip()

    def configure(self):
        # If we're building static then build the world as static, otherwise
        # stuff will probably break.
        # If you want your shared build to also build the world as shared, you
        # can invoke conan with -o "&:shared=False" -o "*:shared=False",
        # possibly with --build=missing or --build=cascade as desired,
        # but this is probably not necessary.
        if not self.options.shared:
            self.options["*"].shared = False

    def requirements(self):
        self.requires("viam-cpp-sdk/0.21.0")
        self.requires("libmp3lame/3.100")
        self.requires("soxr/0.1.3")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def layout(self):
        cmake_layout(self, src_folder=".")

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def deploy(self):
        with TemporaryDirectory(dir=self.deploy_folder) as tmp_dir:
            self.output.debug(f"Creating temporary directory {tmp_dir}")

            self.output.info("Copying audio-module binary")
            copy(self, "audio-module", src=self.package_folder, dst=tmp_dir)

            self.output.info("Copying meta.json")
            copy(self, "meta.json", src=self.package_folder, dst=tmp_dir)

            self.output.info("Copying run.sh")
            copy(self, "run.sh", src=self.package_folder, dst=tmp_dir)

            # Copy bundled libraries if they exist (for Linux runtime dependencies)
            lib_folder = os.path.join(self.package_folder, "lib")
            if os.path.exists(lib_folder):
                self.output.info("Copying bundled libraries from lib/")
                copy(self, "libjack*.so*", src=lib_folder, dst=os.path.join(tmp_dir, "lib"))
                copy(self, "libdb*.so*", src=lib_folder, dst=os.path.join(tmp_dir, "lib"))


            self.output.info("Creating module.tar.gz")
            with tarfile.open(os.path.join(self.deploy_folder, "module.tar.gz"), "w|gz") as tar:
                tar.add(tmp_dir, arcname=".", recursive=True)

                self.output.debug("module.tar.gz contents:")
                for mem in tar.getmembers():
                    self.output.debug(mem.name)
