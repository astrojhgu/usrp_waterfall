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
    (python3.withPackages (ps: with ps; [
    pip
    numpy
    scipy
    matplotlib
    ipython
    sysv_ipc
     ]))
    ];
}
