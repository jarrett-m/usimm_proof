import re
#make all the folders for the new files if they dont exist
import os
#500, 503, 505, 508, 520, 527, 531, 538, 549, 554
#602, 607, 619, 623, 657


new_directories = [
    "input/500.perlbench_rshort",
    "input/503.bwaves_rshort",
    "input/505.mcf_rshort",
    "input/508.namd_rshort",
    "input/520.omnetpp_rshort",
    "input/527.cam4_rshort",
    "input/531.deepsjeng_rshort",
    "input/538.imagick_rshort",
    "input/549.fotonik3d_rshort",
    "input/554.roms_rshort",
    "input/602.gcc_sshort",
    "input/607.cactuBSSN_sshort",
    "input/619.lbm_sshort",
    "input/623.xalancbmk_sshort",
    "input/657.xz_sshort"
]

old_directories = [
    "input/500.perlbench_r",
    "input/503.bwaves_r",
    "input/505.mcf_r",
    "input/508.namd_r",
    "input/520.omnetpp_r",
    "input/527.cam4_r",
    "input/531.deepsjeng_r",
    "input/538.imagick_r",
    "input/549.fotonik3d_r",
    "input/554.roms_r",
    "input/602.gcc_s",
    "input/607.cactuBSSN_s",
    "input/619.lbm_s",
    "input/623.xalancbmk_s",
    "input/657.xz_s"
]



for directory in new_directories:
    try:
        os.makedirs(directory)
        print(f"Folder '{directory}' created successfully.")
    except FileExistsError:
        print(f"Folder '{directory}' already exists.")

for directory in old_directories:
    for i in range(0, 4):
        try:
            with open(f"{directory}/core_{i}") as f:
                with open(f"{directory}short/core_{i}", "w") as f1:
                    for _ in range(2500000):
                        f1.write(f.readline())
            print(f"File '{directory}/core_{i}' copied successfully.")
        except FileNotFoundError:
            print(f"File '{directory}/core_{i}' not found.")

