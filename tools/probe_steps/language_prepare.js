(async () => {
    // A Polish import as the worker leaves it (writeLanguage), then the start layer: the pack's tables, a change to German.
    await Module.writeSettings(withLanguage("", "pl"));
    await fillStartLanguages();
    const select = document.getElementById("start-language");
    const first = {codes: [...select.options].map((o) => o.value).join(), value: select.value};
    select.value = "de";
    select.dispatchEvent(new Event("change"));
    await new Promise((resolve) => setTimeout(resolve, 800));
    return {first, ini: Module.readSettings()};
})()
