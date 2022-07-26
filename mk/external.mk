# For each external target, the following must be defined in advance:
#   _DATA_URL : the hyperlink which points to archive.
#   _DATA : the file to be read by specific executable.
#   _DATA_SHA1 : the checksum of the content in _DATA

# Doom
# https://tipsmake.com/how-to-run-doom-on-raspberry-pi-without-emulator
DOOM_DATA_URL = http://www.doomworld.com/3ddownloads/ports/shareware_doom_iwad.zip
DOOM_DATA = $(OUT)/DOOM1.WAD
DOOM_DATA_SHA1 = 5b2e249b9c5133ec987b3ea77596381dc0d6bc1d

# Quake
QUAKE_DATA_URL = https://www.libsdl.org/projects/quake/data/quakesw-1.0.6.zip
QUAKE_DATA = $(OUT)/id1/pak0.pak
QUAKE_DATA_SHA1 = 36b42dc7b6313fd9cabc0be8b9e9864840929735

define download-n-extract
$($(T)_DATA):
	$(VECHO) "  GET\t$$@\n"
	curl --progress-meter -O -L -C - "$(strip $($(T)_DATA_URL))"
	unzip -d $(OUT) $(notdir $($(T)_DATA_URL))
	echo "$(strip $$($(T)_DATA_SHA1))  $$@" > $$@.sha1
	$(SHA1SUM) -c $$@.sha1
	$(RM) $(notdir $($(T)_DATA_URL))
endef

EXTERNAL_DATA = DOOM QUAKE
$(foreach T,$(EXTERNAL_DATA),$(eval $(download-n-extract))) 
