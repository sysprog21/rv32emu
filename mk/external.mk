# For each external target, the following must be defined in advance:
#   _DATA_URL : the hyperlink which points to archive.
#   _DATA : the file to be read by specific executable.
#   _DATA_SHA1 : the checksum of the content in _DATA
#   _DATA_EXTRACT : the way to extract content from compressed file
#   _DATA_VERIFY : the way to verify the checksum of extracted file

# Doom
# https://tipsmake.com/how-to-run-doom-on-raspberry-pi-without-emulator
DOOM_DATA_URL = http://www.doomworld.com/3ddownloads/ports/shareware_doom_iwad.zip
DOOM_DATA = $(OUT)/DOOM1.WAD
DOOM_DATA_SHA1 = 5b2e249b9c5133ec987b3ea77596381dc0d6bc1d
DOOM_DATA_EXTRACT = unzip -d $(OUT) $(notdir $($(T)_DATA_URL))
DOOM_DATA_VERIFY = echo "$(strip $$($(T)_DATA_SHA1))  $$@" | $(SHA1SUM) -c

# Quake
QUAKE_DATA_URL = https://www.libsdl.org/projects/quake/data/quakesw-1.0.6.zip
QUAKE_DATA = $(OUT)/id1/pak0.pak
QUAKE_DATA_SHA1 = 36b42dc7b6313fd9cabc0be8b9e9864840929735
QUAKE_DATA_EXTRACT = unzip -d $(OUT) $(notdir $($(T)_DATA_URL))
QUAKE_DATA_VERIFY = echo "$(strip $$($(T)_DATA_SHA1))  $$@" | $(SHA1SUM) -c

# Timidity software synthesizer configuration for SDL2_mixer
TIMIDITY_DATA_URL = http://www.libsdl.org/projects/mixer/timidity/timidity.tar.gz
TIMIDITY_DATA = $(OUT)/timidity
TIMIDITY_DATA_SHA1 = cdd30736508d26968222a6414f3beabc3b7a0725
TIMIDITY_DATA_EXTRACT = tar -xf $(notdir $($(T)_DATA_URL)) -C $(OUT)
TIMIDITY_TMP_FILE = /tmp/timidity_sha1.txt
TIMIDITY_DATA_VERIFY = echo "$(TIMIDITY_DATA_SHA1)" > $(TIMIDITY_TMP_FILE) | find $(TIMIDITY_DATA) -type f -print0 | sort -z | xargs -0 $(SHA1SUM) | $(SHA1SUM) | cut -f 1 -d ' '

define download-n-extract
$($(T)_DATA):
	$(VECHO) "  GET\t$$@\n"
	$(Q)curl --progress-bar -O -L -C - "$(strip $($(T)_DATA_URL))"
	$(Q)$($(T)_DATA_EXTRACT)
	$(Q)$($(T)_DATA_VERIFY)
	$(Q)$(RM) $(notdir $($(T)_DATA_URL)) $($(T)_TMP_FILE)
endef

EXTERNAL_DATA = DOOM QUAKE TIMIDITY
$(foreach T,$(EXTERNAL_DATA),$(eval $(download-n-extract)))
