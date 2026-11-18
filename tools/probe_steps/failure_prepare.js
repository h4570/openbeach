(() => {
    // The probe's failure check (failure.json): with every sprite sheet gone from the game copy (MEMFS only, OPFS keeps
    // them), the first frame that draws a sprite ends in FatalError, inside the browser's main loop.
    const sprites = "/game/packs/base/sprites";
    let removed = 0;
    for (const name of FS.readdir(sprites)) {
        const sheet = sprites + "/" + name + "/sheet.png";
        if (FS.analyzePath(sheet).exists) {
            FS.unlink(sheet);
            removed++;
        }
    }
    return {removed};
})()
