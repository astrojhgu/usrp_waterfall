# default.nix
with import <nixpkgs> {};
stdenv.mkDerivation {
    name = "controller"; # Probably put a more meaningful name here
    buildInputs =  [
    pkg-config
    SDL2
    fftwFloat.dev
    boost
    uhd
    libusb
    udev.dev
    libatomic_ops
    freeglut
    freeglut.out
    libsForQt5.qt5.qtwayland

    (python3.withPackages (ps: with ps; [
    pip
    numpy
    scipy
    matplotlib
    ipython
    sysv_ipc
    pyqt5
     ]))
    ];
    LIBCLANG_PATH = llvmPackages.libclang.lib+"/lib";
    LD_LIBRARY_PATH= libGL+"/lib";
    QT_QPA_PLATFORM="wayland";
}
