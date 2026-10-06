#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-tests/data/data/shared_prefs
cc -std=c11 -D_GNU_SOURCE -Itests/host -Isource -Wall -Wextra -Werror -Drename=host_rename -c source/dcr_prefs.c -o build/host-tests/prefs.o
cc -std=c11 -D_GNU_SOURCE -Itests/host -Isource -Wall -Wextra -Werror tests/host/prefs_test.c build/host-tests/prefs.o -lpthread -o build/host-tests/prefs_test
cc -std=c11 -D_GNU_SOURCE -Itests/host -Isource -Wall -Wextra -Werror tests/host/catalog_test.c source/tasm2_catalog.c -lpthread -lm -o build/host-tests/catalog_test
build/host-tests/prefs_test "$PWD/build/host-tests/data" write
build/host-tests/prefs_test "$PWD/build/host-tests/data" read
# Simulate loss of power between retaining the old XML and publishing its replacement.
mv build/host-tests/data/data/shared_prefs/roundtrip.xml build/host-tests/data/data/shared_prefs/roundtrip.xml.bak
build/host-tests/prefs_test "$PWD/build/host-tests/data" read
python3 tests/test_catalog.py

cc -std=c11 -D_GNU_SOURCE -Itests/host -Isource -Iportlibs32/include -Wall -Wextra -Werror -Wno-unused-parameter -ffunction-sections -fdata-sections tests/host/input_test.c source/tasm2_input.c -Wl,--gc-sections -lpthread -lm -o build/host-tests/input_test
build/host-tests/input_test

cc -std=c11 -D_GNU_SOURCE -DHOST_OPENSLES_TEST -DRT_OPENSLES=1 '-DPORT_PAYLOAD_NAME="tasm2_nx"' -Itests/host -Isource -Iruntime/source -Wall -Wextra -Werror -Wno-unused-parameter tests/host/audio_test.c -lpthread -lm -o build/host-tests/audio_test
build/host-tests/audio_test

cc -std=c11 -Itests/host -Wall -Wextra -Werror -Dmalloc=host_malloc -Dfree=host_free -c source/tasm2_installer.c -o build/host-tests/installer.o
cc -std=c11 -Wall -Wextra -Werror tests/host/installer_test.c build/host-tests/installer.o -o build/host-tests/installer_test
build/host-tests/installer_test

cc -std=c11 -DHOST_SPACE_TEST -Itests/host -Wall -Wextra -Werror tests/host/space_test.c source/tasm2_space.c -lm -o build/host-tests/space_test
build/host-tests/space_test

cc -std=c11 -Itests/host -Wall -Wextra -Werror tests/host/warning_test.c source/tasm2_warning.c -o build/host-tests/warning_test
build/host-tests/warning_test

cc -std=c11 -Itests/host -Isource -Wall -Wextra -Werror tests/host/popups_test.c source/tasm2_popups.c -lpthread -o build/host-tests/popups_test
build/host-tests/popups_test

cc -std=c11 -D_GNU_SOURCE -DHOST_OBB_TEST -Itests/host -Isource -Wall -Wextra -Werror tests/host/obb_test.c -lpthread -o build/host-tests/obb_test
build/host-tests/obb_test
cc -std=c11 -Itests/host -Isource -Iportlibs32/include -Wall -Wextra -Werror tests/host/fence_test.c -lpthread -o build/host-tests/fence_test
build/host-tests/fence_test

cc -std=c11 -D_GNU_SOURCE -Itests/host -Isource -Iruntime/source -Wall -Wextra -Werror tests/host/save_test.c -lpthread -o build/host-tests/save_test
build/host-tests/save_test
