Scriptname F4A:SoundHelper
{Helper for playing sounds from F4SE plugin via Papyrus VM.
 Accepts form ID as int to avoid C++ PackVariable crash when
 binding BGSSoundDescriptorForm to a Papyrus Sound object.}

int Function PlaySound(int aiSoundFormID) global
    Sound snd = Game.GetForm(aiSoundFormID) as Sound
    if snd
        return snd.Play(Game.GetPlayer())
    endIf
    return 0
EndFunction

Function StopSound(int aiInstanceID) global
    if aiInstanceID > 0
        Sound.StopInstance(aiInstanceID)
    endIf
EndFunction
