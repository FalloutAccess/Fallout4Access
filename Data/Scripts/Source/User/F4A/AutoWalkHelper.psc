Scriptname F4A:AutoWalkHelper
{Bridge between the F4SE plugin and the F4A:AutoWalk quest.

 The plugin calls WalkPlayerTo/StopWalk via DispatchStaticCall (a GLOBAL
 function call — the only C++->Papyrus path that works on the OG 1.10.163
 runtime). This global then drives the player through the quest's Scene + Travel
 package, the proven way to move the PLAYER (Actor.PathToReference does not path
 the player on OG). Papyrus-to-Papyrus calls are unaffected by the OG C++-dispatch
 limitations, so the quest's latent Scene logic runs normally.

 Requires fallout4access.esp active so F4A_AutoWalkQuest (0x000803) binds.}

; Resolve the bound F4A:AutoWalk quest script regardless of load-order index.
F4A:AutoWalk Function GetWalkQuest() global
    return Game.GetFormFromFile(0x000803, "fallout4access.esp") as F4A:AutoWalk
EndFunction

; Start walking the player to the target reference (passed as an int form ID to
; avoid C++ ref-packing). Routes to the quest's Scene/Travel package.
Function WalkPlayerTo(int aiTargetFormID) global
    ObjectReference target = Game.GetForm(aiTargetFormID) as ObjectReference
    if !target
        return
    endIf
    F4A:AutoWalk walkQuest = GetWalkQuest()
    if walkQuest
        walkQuest.StartWalkingTo(target)
    endIf
EndFunction

; Stop an in-progress walk and hand control back to the player.
Function StopWalk() global
    F4A:AutoWalk walkQuest = GetWalkQuest()
    if walkQuest
        walkQuest.StopWalking(false)
    endIf
EndFunction
