# Mixology Bridge for Ableton Live 11 / 12: lets the Mixology plug-in (Sonicology) read and move Live's track
# volumes, pans, mute and solo, the way it does in Cubase.
#
# Install: this folder ("MixologyBridge") goes into Live's User Library, in "Remote Scripts":
#   Windows  Documents\Ableton\User Library\Remote Scripts\MixologyBridge\
#   Mac      ~/Music/Ableton/User Library/Remote Scripts/MixologyBridge/
# The Sonicology installer puts it there. Restart Live, then Settings > Link, Tempo & MIDI: a Control Surface
# "MixologyBridge", Input "Mixology to Cubase", Output "Cubase to Mixology".
# (Windows: the two ports come from loopMIDI; on a Mac Mixology makes them itself.)

from .bridge import MixologyBridge


def create_instance(c_instance):
    return MixologyBridge(c_instance)
