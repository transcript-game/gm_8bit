# gm_8bit
A module for relaying Steam voice packets from Garry's Mod.

# What does it do?
gm_8bit hooks Source Engine voice traffic and forwards the raw Steam voice packets over UDP. It is designed for piping in-game voice to external services for recording, analysis, or restreaming.

gm_8bit intercepts `SV_BroadcastVoiceData`, stamps each packet with the player's SteamID64, and relays it to a configurable IP/port. No in-engine audio effects or Opus processing remain—packets are forwarded as-is.

gm_8bit ships with a reference relay implementation. See the `voice-relay` repository for an example server that uses gm_8bit to relay server voice communications to a discord channel.

# Builds
Both windows and linux builds are available with every commit. See the actions page.

# API
`eightbit.EnableBroadcast(bool)` Sets whether the module should relay voice packets. Defaults to `true` on load.

`eightbit.SetBroadcastIP(string)` Controls what IP the module should relay voice packets to, if broadcast is enabled.

`eightbit.SetBroadcastPort(number)` Controls what port the module should relay voice packets to, if broadcast is enabled.
