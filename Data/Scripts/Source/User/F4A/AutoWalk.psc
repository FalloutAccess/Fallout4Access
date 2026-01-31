Scriptname F4A:AutoWalk extends Quest conditional
{
  Auto-walk system for Fallout4Access scanner.
  F4SE plugin calls OnWalkToTarget/OnStopWalking via DispatchMethodCall,
  passing FormIDs as proper int32 (no float precision loss from globals).

  Setup:
  1. Attach this script to F4A_AutoWalkQuest
  2. Set the property values in Creation Kit
  3. F4SE plugin calls OnWalkToTarget(formID, stopDistance) directly
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

float fStopDistance = 100.0

; Timer ID for arrival checking
int TimerArrivalCheck = 1 const

; === Events ===

Event OnInit()
    Debug.Trace("F4A:AutoWalk initialized")
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

; === Functions called from C++ via DispatchMethodCall ===

Function OnWalkToTarget(int aiFormID, float afStopDistance)
    {Called by F4SE plugin - receives FormID as proper int32, no float precision loss}

    Form targetForm = Game.GetForm(aiFormID)

    if targetForm
        ObjectReference targetRef = targetForm as ObjectReference
        if targetRef
            fStopDistance = afStopDistance
            StartWalkingTo(targetRef)
        else
            Debug.Trace("F4A:AutoWalk - Form is not an ObjectReference: " + aiFormID)
        endIf
    else
        Debug.Trace("F4A:AutoWalk - Invalid FormID from plugin: " + aiFormID)
    endIf
EndFunction

Function OnStopWalking()
    {Called by F4SE plugin to stop walking}

    if IsWalking
        Debug.Trace("F4A:AutoWalk - Stop signal received from plugin")
        StopWalking(false)
    endIf
EndFunction

; === Walking Functions ===

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
    float stopDist = fStopDistance
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

Event OnTimer(int aiTimerID)
    if aiTimerID == TimerArrivalCheck
        if IsWalking
            CheckArrival()
        endIf
    endIf
EndEvent

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

    float stopDist = fStopDistance
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
