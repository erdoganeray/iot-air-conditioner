# PlatformIO derleme sonrası betiği: önyükleyici + bölüm tablosu + uygulamayı tek dosyada
# birleştirir (.pio/build/<ortam>/firmware-full.bin). Bu dosya tarayıcıdan (ESP Web Flasher /
# esptool-js) 0x0 adresine tek seferde yüklenebilir; PlatformIO gerekmez.
Import("env")  # noqa: F821  (PlatformIO tarafından sağlanır)

import os  # noqa: E402


def merge_bin(source, target, env):
    build = env.subst("$BUILD_DIR")
    out = os.path.join(build, "firmware-full.bin")
    app = os.path.join(build, env.subst("${PROGNAME}.bin"))
    images = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [offset, env.subst(path)]
    images += [env.subst("$ESP32_APP_OFFSET"), app]
    uploader = env.subst("$UPLOADER")
    # esptool bazı sürümlerde Python betiği, bazılarında derlenmiş programdır
    prefix = [env.subst("$PYTHONEXE")] if uploader.endswith(".py") else []
    cmd = prefix + [
        uploader, "--chip", env.subst("$BOARD_MCU"), "merge_bin",
        "-o", out,
        "--flash_mode", env.subst("${__get_board_flash_mode(__env__)}"),
        "--flash_freq", env.subst("${__get_board_f_image(__env__)}"),
        "--flash_size", env.BoardConfig().get("upload.flash_size", "4MB"),
    ] + images
    if env.Execute(" ".join('"%s"' % c if " " in c else c for c in cmd)):
        env.Exit(1)
    print("Birlesik dosya (0x0 adresine yuklenir):", out)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)  # noqa: F821
