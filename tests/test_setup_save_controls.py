from pathlib import Path


source = Path(
    "source/ESP32-S3/V1.7/U1_Argus_Remote_RFID_ESP32-S3_N16R8_V1_7.ino"
).read_text()

start = source.index("static String configPageHtml(const String& msg) {")
end = source.index("static String dashboardPageHtml()", start)
setup = source[start:end]
setup = setup.replace('\"', '"')
setup = setup.replace("\'", "'")

form_open = setup.index("<form method='POST' action='/save'>")
form_close = setup.index("</form>", form_open)
form = setup[form_open:form_close]

assert "name='debug_serial'" in form
assert form.count("type='submit' class='alt'") >= 3
assert "saveTop" in form
assert "saveDock" in form
assert form.index("saveTop") < form.index("name='debug_serial'")
assert form.index("name='debug_serial'") < form.index("saveDock")
assert "position:fixed" in setup
assert "bottom:0" in setup
assert "Save settings &amp; reboot" in form

print("PASS: setup save controls are visible above, below and fixed on screen")
