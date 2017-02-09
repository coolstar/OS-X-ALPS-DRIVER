//
// Created by Brandon Pedersen on 5/1/13.
//

#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include "VoodooPS2Controller.h"
#include "VoodooPS2TouchPadBase.h"

// =============================================================================
// VoodooPS2TouchPadBase Class Implementation
//

OSDefineMetaClassAndAbstractStructors(VoodooPS2TouchPadBase, IOHIPointing);

UInt32 VoodooPS2TouchPadBase::deviceType()
{ return NX_EVS_DEVICE_TYPE_MOUSE; };

UInt32 VoodooPS2TouchPadBase::interfaceID()
{ return NX_EVS_DEVICE_INTERFACE_BUS_ACE; };

IOItemCount VoodooPS2TouchPadBase::buttonCount() { return _buttonCount; };
IOFixed     VoodooPS2TouchPadBase::resolution()  { return (300) << 16; };

#define abs(x) ((x) < 0 ? -(x) : (x))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool VoodooPS2TouchPadBase::init(OSDictionary * dict)
{
    //
    // Initialize this object's minimal state. This is invoked right after this
    // object is instantiated.
    //
    
    if (!super::init(dict))
        return false;

    // find config specific to Platform Profile
    OSDictionary* list = OSDynamicCast(OSDictionary, dict->getObject(kPlatformProfile));
    OSDictionary* config = ApplePS2Controller::makeConfigurationNode(list);
    if (config)
    {
        // if DisableDevice is Yes, then do not load at all...
        OSBoolean* disable = OSDynamicCast(OSBoolean, config->getObject(kDisableDevice));
        if (disable && disable->isTrue())
        {
            config->release();
            return false;
        }
#ifdef DEBUG
        // save configuration for later/diagnostics...
        setProperty(kMergedConfiguration, config);
#endif
    }
    
    // initialize state...
    _device = NULL;
    _interruptHandlerInstalled = false;
    _powerControlHandlerInstalled = false;
    _messageHandlerInstalled = false;
    _packetByteCount = 0;
    _lastdata = 0;
    _cmdGate = 0;

    // set defaults for configuration items
    
	z_finger=45;
    rtap=true;
    noled = false;
    maxaftertyping = 500000000;
    _resolution = 2300;
    _scrollresolution = 800;
    swipedx = swipedy = 800;
    _buttonCount = 2;

    xupmm = yupmm = 50; // 50 is just arbitrary, but same
    
    _extendedwmode=false;
    
    // intialize state
    
	lastx=0;
	lasty=0;
    lastbuttons=0;
    
    // intialize state for secondary packets/extendedwmode
    xrest2=0;
    yrest2=0;
    clickedprimary=false;
    lastx2=0;
    lasty2=0;
    tracksecondary=false;
    
    // state for middle button
    _mbuttonstate = STATE_NOBUTTONS;
    _pendingbuttons = 0;
    _buttontime = 0;
    _maxmiddleclicktime = 100000000;
    _fakemiddlebutton = true;
    
    ignoredeltas=0;
    ignoredeltasstart=0;
    touchtime=untouchtime=0;
	wastriple=wasdouble=false;
    keytime = 0;
    ignoreall = false;
    passbuttons = 0;
    passthru = false;
    ledpresent = false;
    clickpadtype = 0;
    _clickbuttons = 0;
    _reportsv = false;
    mousecount = 0;
    usb_mouse_stops_trackpad = true;
    _modifierdown = 0;
    scrollzoommask = 0;
    
	touchmode=MODE_NOTOUCH;
    
	IOLog("VoodooPS2TouchPad loaded...\n");
    
	setProperty("Revision", 24, 32);
    
    //
    // Load settings specific to Platform Profile
    //
    
    setParamPropertiesGated(config);
    OSSafeReleaseNULL(config);
    
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool VoodooPS2TouchPadBase::start( IOService * provider )
{
    //
    // The driver has been instructed to start. This is called after a
    // successful probe and match.
    //

    if (!super::start(provider))
        return false;

    //
    // Maintain a pointer to and retain the provider object.
    //

    _device = (ApplePS2MouseDevice *) provider;
    _device->retain();
    
    //
    // Advertise the current state of the tapping feature.
    //
    // Must add this property to let our superclass know that it should handle
    // trackpad acceleration settings from user space.  Without this, tracking
    // speed adjustments from the mouse prefs panel have no effect.
    //

    setProperty(kIOHIDPointerAccelerationTypeKey, kIOHIDTrackpadAccelerationType);
    setProperty(kIOHIDScrollAccelerationTypeKey, kIOHIDTrackpadScrollAccelerationKey);
	setProperty(kIOHIDScrollResolutionKey, 800 << 16, 32);
    
    //
    // Setup workloop with command gate for thread synchronization...
    //
    IOWorkLoop* pWorkLoop = getWorkLoop();
    _cmdGate = IOCommandGate::commandGate(this);
    if (!pWorkLoop || !_cmdGate)
    {
        _device->release();
        return false;
    }
    
    //
    // Lock the controller during initialization
    //
    
    _device->lock();

    //
    // Perform any implementation specific device initialization
    //
    if (!deviceSpecificInit()) {
        _device->unlock();
        _device->release();
        // TODO: any other cleanup?
        return false;
    }
    
    _xraw1 = _xraw2 = _yraw1 = _yraw2 = _fingerCount = -1;
    _buttonDown = false;
    
    //
    // Setup scrolltimer event source
    //
    
    softc.settings.multiFingerTap = false;
    softc.settings.tapToClickEnabled = false;
    softc.settings.tapDragEnabled = false;
    
    softc.lastlegacycount = 0;
    softc.legacycount = 0;
    
    _csgesture = new CSGesture;
    _csgesture->softc = &softc;
    _csgesture->_pointingWrapper = this;
    _csgesture->initialize_wrapper(this);
    
    _gestureTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &VoodooPS2TouchPadBase::onGestureTimer));
    if (_gestureTimer){
        pWorkLoop->addEventSource(_gestureTimer);
        _gestureTimer->setTimeoutMS(10);
    }

    //
    // Install our driver's interrupt handler, for asynchronous data delivery.
    //
    
    _device->installInterruptAction(this,
                                    OSMemberFunctionCast(PS2InterruptAction,this,&VoodooPS2TouchPadBase::interruptOccurred),
                                    OSMemberFunctionCast(PS2PacketAction, this, &VoodooPS2TouchPadBase::packetReady));
    _interruptHandlerInstalled = true;
    
    // now safe to allow other threads
    _device->unlock();
    
    //
	// Install our power control handler.
	//
    
	_device->installPowerControlAction( this,
        OSMemberFunctionCast(PS2PowerControlAction, this, &VoodooPS2TouchPadBase::setDevicePowerState) );
	_powerControlHandlerInstalled = true;
    
    //
    // Install message hook for keyboard to trackpad communication
    //
    
    _device->installMessageAction( this,
        OSMemberFunctionCast(PS2MessageAction, this, &VoodooPS2TouchPadBase::receiveMessage));
    _messageHandlerInstalled = true;

    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void VoodooPS2TouchPadBase::stop( IOService * provider )
{
    DEBUG_LOG("%s: stop called\n", getName());
    
    //
    // The driver has been instructed to stop.  Note that we must break all
    // connections to other service objects now (ie. no registered actions,
    // no pointers and retains to objects, etc), if any.
    //

    assert(_device == provider);

    // free up timer for scroll momentum
    IOWorkLoop* pWorkLoop = getWorkLoop();
    if (pWorkLoop)
    {
        if (_gestureTimer)
        {
            pWorkLoop->removeEventSource(_gestureTimer);
            _gestureTimer->release();
            _gestureTimer = 0;
        }
        if (_cmdGate)
        {
            pWorkLoop->removeEventSource(_cmdGate);
            _cmdGate->release();
            _cmdGate = 0;
        }
    }
    
    if (_csgesture){
        _csgesture->destroy_wrapper();
        _csgesture = 0;
    }
    
    //
    // Uninstall the interrupt handler.
    //

    if (_interruptHandlerInstalled)
    {
        _device->uninstallInterruptAction();
        _interruptHandlerInstalled = false;
    }

    //
    // Uninstall the power control handler.
    //

    if (_powerControlHandlerInstalled)
    {
        _device->uninstallPowerControlAction();
        _powerControlHandlerInstalled = false;
    }
    
    //
    // Uinstall message handler.
    //
    if (_messageHandlerInstalled)
    {
        _device->uninstallMessageAction();
        _messageHandlerInstalled = false;
    }

    //
    // Release the pointer to the provider object.
    //
    
    OSSafeReleaseNULL(_device);
    
	super::stop(provider);
}



// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void VoodooPS2TouchPadBase::initTouchPad()
{
    //
    // Clear packet buffer pointer to avoid issues caused by
    // stale packet fragments.
    //
    
    _packetByteCount = 0;
    _ringBuffer.reset();
    
    // clear passbuttons, just in case buttons were down when system
    // went to sleep (now just assume they are up)
    passbuttons = 0;
    _clickbuttons = 0;
    tracksecondary=false;
    
    // clear state of control key cache
    _modifierdown = 0;
    
    // initialize the touchpad
    deviceSpecificInit();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void VoodooPS2TouchPadBase::setParamPropertiesGated(OSDictionary * config)
{
	if (NULL == config)
		return;
    
	const struct {const char *name; int *var;} int32vars[]={
		{"FingerZ",							&z_finger},
        {"Resolution",                      &_resolution},
        {"ScrollResolution",                &_scrollresolution},
        {"SwipeDeltaX",                     &swipedx},
        {"SwipeDeltaY",                     &swipedy},
        {"MouseCount",                      &mousecount},
        {"HIDScrollZoomModifierMask",       &scrollzoommask},
        {"ButtonCount",                     &_buttonCount},
        {"FingerChangeIgnoreDeltas",        &ignoredeltasstart},
        {"UnitsPerMMX",                     &xupmm},
        {"UnitsPerMMY",                     &yupmm},
	};
	const struct {const char *name; int *var;} boolvars[]={
        {"DisableLEDUpdate",                &noled},
        {"FakeMiddleButton",                &_fakemiddlebutton},
	};
    const struct {const char* name; bool* var;} lowbitvars[]={
        {"TrackpadRightClick",              &rtap},
        {"USBMouseStopsTrackpad",           &usb_mouse_stops_trackpad},
        {"TrackpadMomentumScroll",          &momentumscroll},
    };
    const struct {const char* name; uint64_t* var; } int64vars[]={
        {"QuietTimeAfterTyping",            &maxaftertyping},
        {"MiddleClickTime",                 &_maxmiddleclicktime},
    };
    
    int oldmousecount = mousecount;
    bool old_usb_mouse_stops_trackpad = usb_mouse_stops_trackpad;

    OSBoolean *bl;
    OSNumber *num;
    // 64-bit config items
    for (int i = 0; i < countof(int64vars); i++) {
        if ((num=OSDynamicCast(OSNumber, config->getObject(int64vars[i].name))))
        {
            *int64vars[i].var = num->unsigned64BitValue();
            ////DEBUG_LOG("%s::setProperty64(%s, %llu)\n", getName(), int64vars[i].name, *int64vars[i].var);
            setProperty(int64vars[i].name, *int64vars[i].var, 64);
        }
    }
    // boolean config items
	for (int i = 0; i < countof(boolvars); i++) {
		if ((bl=OSDynamicCast (OSBoolean,config->getObject (boolvars[i].name))))
        {
			*boolvars[i].var = bl->isTrue();
            ////DEBUG_LOG("%s::setPropertyBool(%s, %d)\n", getName(), boolvars[i].name, *boolvars[i].var);
            setProperty(boolvars[i].name, *boolvars[i].var ? kOSBooleanTrue : kOSBooleanFalse);
        }
    }
    // 32-bit config items
	for (int i = 0; i < countof(int32vars);i++) {
		if ((num=OSDynamicCast (OSNumber,config->getObject (int32vars[i].name))))
        {
			*int32vars[i].var = num->unsigned32BitValue();
            ////DEBUG_LOG("%s::setProperty32(%s, %d)\n", getName(), int32vars[i].name, *int32vars[i].var);
            setProperty(int32vars[i].name, *int32vars[i].var, 32);
        }
    }
    // lowbit config items
	for (int i = 0; i < countof(lowbitvars); i++) {
		if ((num=OSDynamicCast (OSNumber,config->getObject(lowbitvars[i].name))))
        {
			*lowbitvars[i].var = (num->unsigned32BitValue()&0x1)?true:false;
            ////DEBUG_LOG("%s::setPropertyLowBit(%s, %d)\n", getName(), lowbitvars[i].name, *lowbitvars[i].var);
            setProperty(lowbitvars[i].name, *lowbitvars[i].var ? 1 : 0, 32);
        }
    }

//REVIEW: this should be done maybe only when necessary...
    touchmode=MODE_NOTOUCH;

    // check for special terminating sequence from PS2Daemon
    if (-1 == mousecount)
    {
        DEBUG_LOG("Shutdown touchpad, mousecount=%d\n", mousecount);
        touchpadShutdown();
        mousecount = oldmousecount;
    }

    // disable trackpad when USB mouse is plugged in
    // check for mouse count changing...
    if ((oldmousecount != 0) != (mousecount != 0) || old_usb_mouse_stops_trackpad != usb_mouse_stops_trackpad)
    {
        // either last mouse removed or first mouse added
        ignoreall = (mousecount != 0) && usb_mouse_stops_trackpad;
        touchpadToggled();
    }
}

IOReturn VoodooPS2TouchPadBase::setParamProperties(OSDictionary* dict)
{
    ////IOReturn result = super::IOHIDevice::setParamProperties(dict);
    if (_cmdGate)
    {
        // syncronize through workloop...
        ////_cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &VooodooPS2TouchPadBase::setParamPropertiesGated), dict);
        setParamPropertiesGated(dict);
    }
    
    return super::setParamProperties(dict);
    ////return result;
}

IOReturn VoodooPS2TouchPadBase::setProperties(OSObject *props)
{
	OSDictionary *dict = OSDynamicCast(OSDictionary, props);
    if (dict && _cmdGate)
    {
        // syncronize through workloop...
        _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &VoodooPS2TouchPadBase::setParamPropertiesGated), dict);
    }
    
	return super::setProperties(props);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void VoodooPS2TouchPadBase::setDevicePowerState( UInt32 whatToDo )
{
    switch ( whatToDo )
    {
        case kPS2C_DisableDevice:
            //
            // Disable touchpad (synchronous).
            //
            
            cancelTimer(_gestureTimer);
            
            if (_csgesture)
                _csgesture->prepareToSleep();

            setTouchPadEnable( false );
            break;

        case kPS2C_EnableDevice:
            //
            // Must not issue any commands before the device has
            // completed its power-on self-test and calibration.
            //

            IOSleep(1000);
            
            // Reset and enable the touchpad.
            initTouchPad();
            
            _gestureTimer->setTimeoutMS(10);
            
            if (_csgesture)
                _csgesture->wakeFromSleep();
            
            break;
    }
}

void VoodooPS2TouchPadBase::updateRelativeMouse(int dx, int dy, int buttons){
    // 0x1 = left button
    // 0x2 = right button
    // 0x4 = middle button
    
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    dispatchRelativePointerEvent(dx, dy, buttons, now_abs);
}

void VoodooPS2TouchPadBase::updateScroll(short dy, short dx, short dz){
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    //if (!horizontalScroll)
    //    dx = 0;
    dispatchScrollWheelEvent(dy, dx, dz, now_abs);
}

void VoodooPS2TouchPadBase::updateKeyboard(char keyCode){
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    if (keyCode == 0x52){
        _device->dispatchKeyboardMessage(kPS2M_swipeUp, &now_abs);
    } else if (keyCode == 0x51){
        _device->dispatchKeyboardMessage(kPS2M_swipeDown, &now_abs);
    } else if (keyCode == 0x4F){
        _device->dispatchKeyboardMessage(kPS2M_swipeLeft, &now_abs);
    } else if (keyCode == 0x50){
        _device->dispatchKeyboardMessage(kPS2M_swipeRight, &now_abs);
    }
}

void VoodooPS2TouchPadBase::onGestureTimer(){
    softc.lastlegacycount = softc.legacycount;
    softc.enableLegacyMode = true;
    
    softc.legacycount = _fingerCount;
    softc.legacyx[0] = _xraw1;
    softc.legacyy[0] = _yraw1;
    
    softc.legacyx[1] = _xraw2;
    softc.legacyy[1] = _yraw2;
    
    softc.buttondown = _buttonDown;
    
    _csgesture->LegacyProcessGesture(&softc);
    
    _gestureTimer->setTimeoutMS(10);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void VoodooPS2TouchPadBase::receiveMessage(int message, void* data)
{
    //
    // Here is where we receive messages from the keyboard driver
    //
    // This allows for the keyboard driver to enable/disable the trackpad
    // when a certain keycode is pressed.
    //
    // It also allows the trackpad driver to learn the last time a key
    //  has been pressed, so it can implement various "ignore trackpad
    //  input while typing" options.
    //
    switch (message)
    {
        case kPS2M_getDisableTouchpad:
        {
            bool* pResult = (bool*)data;
            *pResult = !ignoreall;
            break;
        }
            
        case kPS2M_setDisableTouchpad:
        {
            bool enable = *((bool*)data);
            // ignoreall is true when trackpad has been disabled
            if (enable == ignoreall)
            {
                // save state, and update LED
                ignoreall = !enable;
                touchpadToggled();
            }
            break;
        }
            
        case kPS2M_notifyKeyPressed:
        {
            // just remember last time key pressed... this can be used in
            // interrupt handler to detect unintended input while typing
            PS2KeyInfo* pInfo = (PS2KeyInfo*)data;
            static const int masks[] =
            {
                0x10,       // 0x36
                0x100000,   // 0x37
                0,          // 0x38
                0,          // 0x39
                0x080000,   // 0x3a
                0x040000,   // 0x3b
                0,          // 0x3c
                0x08,       // 0x3d
                0x04,       // 0x3e
                0x200000,   // 0x3f
            };
#ifdef SIMULATE_PASSTHRU
            static int buttons = 0;
            int button;
            switch (pInfo->adbKeyCode)
            {
                // make right Alt,Menu,Ctrl into three button passthru
                case 0x36:
                    button = 0x1;
                    goto dispatch_it;
                case 0x3f:
                    button = 0x4;
                    goto dispatch_it;
                case 0x3e:
                    button = 0x2;
                    // fall through...
                dispatch_it:
                    if (pInfo->goingDown)
                        buttons |= button;
                    else
                        buttons &= ~button;
                    UInt8 packet[6];
                    packet[0] = 0x84 | trackbuttons;
                    packet[1] = 0x08 | buttons;
                    packet[2] = 0;
                    packet[3] = 0xC4 | trackbuttons;
                    packet[4] = 0;
                    packet[5] = 0;
                    dispatchEventsWithPacket(packet, 6);
                    pInfo->eatKey = true;
            }
#endif
            switch (pInfo->adbKeyCode)
            {
                // don't store key time for modifier keys going down
                // track modifiers for scrollzoom feature...
                // (note: it turns out we didn't need to do this, but leaving this code in for now in case it is useful)
                case 0x38:  // left shift
                case 0x3c:  // right shift
                case 0x3b:  // left control
                case 0x3e:  // right control
                case 0x3a:  // left windows (option)
                case 0x3d:  // right windows
                case 0x37:  // left alt (command)
                case 0x36:  // right alt
                case 0x3f:  // osx fn (function)
                    if (pInfo->goingDown)
                    {
                        _modifierdown |= masks[pInfo->adbKeyCode-0x36];
                        break;
                    }
                    _modifierdown &= ~masks[pInfo->adbKeyCode-0x36];
                    keytime = pInfo->time;
                    break;
                    
                default:
                    //TODO: CANCEL MOMENTUM CURRENT HERE
                    //momentumscrollcurrent = 0;  // keys cancel momentum scroll
                    keytime = pInfo->time;
            }
            break;
        }
    }
}
