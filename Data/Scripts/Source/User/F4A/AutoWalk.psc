Scriptname F4A:AutoWalk extends Quest conditional
{
  Auto-walk system for Fallout4Access scanner.
  Walks player to a target object set by the F4SE plugin via GlobalVariable.

  Setup:
  1. Attach this script to F4A_AutoWalkQuest
  2. Set the property values in Creation Kit
  3. F4SE plugin writes target FormID to F4A_TargetFormID global
}

; === Properties (set in Creation Kit) ===

group Aliases
    ReferenceAlias property DstMarker auto const mandatory
    {Destination marker alias - target gets forced here}

    ReferenceAlias property Traveler auto const mandatory
    {Player alias for Scene - fill with PlayerRef in CK}
endGroup

group Scene
    Scene property WalkScene auto const mandatory
    {The walking scene - controls AI-driven movement with proper animation}
endGroup

group References
    Actor property PlayerRef auto const mandatory
    {Player reference - usually Game.GetPlayer()}

    GlobalVariable property F4A_TargetFormID auto const mandatory
    {Global storing target FormID from F4SE plugin}

    GlobalVariable property F4A_StopDistance auto const mandatory
    {Global storing how close to stop (default 100)}

    Idle property IdleStop auto const
    {Optional idle to play when stopping}
endGroup

; === State Variables ===

bool property IsWalking = false auto hidden conditional
{True while auto-walking}

ObjectReference property CurrentTarget = None auto hidden
{The current walk target}

float property CheckInterval = 0.25 auto hidden
{How often to check arrival (seconds)}

; Timer ID for arrival checking
int TimerArrivalCheck = 1 const

; Timer ID for polling GlobalVariable
int TimerTargetPoll = 2 const

; === Events ===

Event OnInit()
    ; Start polling for target from F4SE plugin
    Debug.Trace("F4A:AutoWalk initialized, polling for targets")
    StartTimer(0.5, TimerTargetPoll)
EndEvent

Event OnTimer(int aiTimerID)
    if aiTimerID == TimerTargetPoll
        CheckForNewTarget()
        ; Continue polling
        StartTimer(0.1, TimerTargetPoll)

    elseIf aiTimerID == TimerArrivalCheck
        if IsWalking
            CheckArrival()
        endIf
    endIf
EndEvent

Event Actor.OnCombatStateChanged(Actor akSender, Actor akTarget, int aeCombatState)
    ; 0 = Not in combat, 1 = In combat, 2 = Searching
    if akSender == PlayerRef && aeCombatState == 1
        if IsWalking
            Debug.Notification("Combat detected - stopping walk")
            Debug.Trace("F4A:AutoWalk - Combat started, stopping")
            StopWalking(true)
        endIf
    endIf
EndEvent

; === Main Functions ===

Function CheckForNewTarget()
    {Check if F4SE plugin set a new target FormID or stop signal}

    float targetFormID = F4A_TargetFormID.GetValue()

    ; Check for stop signal (-1) from C++
    if targetFormID < 0
        F4A_TargetFormID.SetValue(0)
        if IsWalking
            Debug.Trace("F4A:AutoWalk - Stop signal received from plugin")
            StopWalking(false)
        endIf
        return
    endIf

    if targetFormID > 0
        ; Get the actual reference from FormID
        ; The FormID is a runtime ID from the scanner (NPC, door, container, etc.)
        int formID = targetFormID as int
        Form targetForm = Game.GetForm(formID)

        if targetForm
            ObjectReference targetRef = targetForm as ObjectReference
            if targetRef && targetRef != CurrentTarget
                ; Clear the global so we don't re-trigger
                F4A_TargetFormID.SetValue(0)

                ; Start walking to this target
                StartWalkingTo(targetRef)
            endIf
        else
            ; Invalid FormID, clear it
            F4A_TargetFormID.SetValue(0)
            Debug.Trace("F4A:AutoWalk - Invalid FormID: " + formID)
        endIf
    endIf
EndFunction

Function StartWalkingTo(ObjectReference akTarget)
    {Begin walking to the specified target}

    if !akTarget
        Debug.Trace("F4A:AutoWalk - StartWalkingTo: Invalid target")
        return
    endIf

    if IsWalking
        ; Already walking - stop first
        StopWalkingInternal()
    endIf

    CurrentTarget = akTarget
    float stopDist = F4A_StopDistance.GetValue()
    if stopDist <= 0
        stopDist = 100.0
    endIf

    ; Check if already close enough
    float distance = PlayerRef.GetDistance(akTarget)
    if distance <= stopDist
        Debug.Trace("F4A:AutoWalk - Already within range: " + distance)
        Debug.Notification("Already close to target")
        CurrentTarget = None
        return
    endIf

    Debug.Trace("F4A:AutoWalk - Starting walk, distance: " + distance)
    Debug.Notification("Walking to target")

    ; Force target into destination alias - Scene's Travel package uses this
    DstMarker.ForceRefTo(akTarget)

    ; Register for combat events
    RegisterForRemoteEvent(PlayerRef, "OnCombatStateChanged")

    ; Enable AI-driven movement
    Game.SetPlayerAIDriven(true)

    ; Start the walking scene - this handles movement with proper animation/audio
    WalkScene.Start()

    IsWalking = true

    ; Start arrival check timer
    StartTimer(CheckInterval, TimerArrivalCheck)
EndFunction

Function CheckArrival()
    {Check if we've arrived at destination}

    if !IsWalking || !CurrentTarget
        return
    endIf

    ; Check if target is still valid
    if CurrentTarget.IsDeleted() || CurrentTarget.IsDisabled()
        Debug.Trace("F4A:AutoWalk - Target became invalid")
        Debug.Notification("Target no longer available")
        StopWalking(false)
        return
    endIf

    float stopDist = F4A_StopDistance.GetValue()
    if stopDist <= 0
        stopDist = 100.0
    endIf

    float distance = PlayerRef.GetDistance(CurrentTarget)

    ; Add some tolerance
    if distance <= stopDist + 20.0
        Debug.Trace("F4A:AutoWalk - Arrived, distance: " + distance)
        Debug.Notification("Arrived")
        StopWalking(false)

        ; Look at target
        PlayerRef.SetLookAt(CurrentTarget, true)
    else
        ; Continue checking
        StartTimer(CheckInterval, TimerArrivalCheck)
    endIf
EndFunction

Function StopWalking(bool abNotify = true)
    {Stop walking and restore player control}

    if abNotify
        Debug.Notification("Stopped walking")
    endIf

    StopWalkingInternal()
EndFunction

Function StopWalkingInternal()
    {Internal stop logic}

    CancelTimer(TimerArrivalCheck)

    ; Stop the walking scene
    WalkScene.Stop()

    ; Unregister combat events
    UnregisterForRemoteEvent(PlayerRef, "OnCombatStateChanged")

    ; Restore player control
    Game.SetPlayerAIDriven(false)
    PlayerRef.EvaluatePackage()

    ; Play stop idle if available
    if IdleStop
        PlayerRef.PlayIdle(IdleStop)
    endIf

    ; Clear state
    DstMarker.Clear()
    CurrentTarget = None
    IsWalking = false

    Debug.Trace("F4A:AutoWalk - Stopped")
EndFunction

; === Utility Functions ===

Function ForceStop()
    {Force stop from external call (console command, etc)}

    if IsWalking
        StopWalking(true)
    endIf
EndFunction
