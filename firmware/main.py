"""Motor Bench v3. JSON serial; explicit motion, 1.5s host lease, 2s watchdog."""
from machine import Pin, I2C, PWM, WDT
from time import ticks_ms, ticks_diff, ticks_add, sleep_ms
import sys, select, json, os, math
import machine, device_config
device_name=device_config.load_name()
reset_at=None

led=Pin(25,Pin.OUT,value=0)
enable=Pin(22,Pin.OUT,value=0)
coils=[Pin(n,Pin.OUT,value=0) for n in (18,19,20,21)]
from tmp102 import TMP102
alert=Pin(24,Pin.IN); fault=Pin(23,Pin.IN)
buzzer=PWM(Pin(16));buzzer.freq(2731);buzzer.duty_u16(0)
threshold=65.0
try:
    with open('alarm.json') as f: value=json.load(f)['threshold_c']
    if isinstance(value,(int,float)) and math.isfinite(value) and 10<=value<=74: threshold=float(value)
except Exception: pass
sensor=None
try:
    sensor=TMP102(I2C(1,sda=Pin(26),scl=Pin(27),freq=100000));sensor.configure();sleep_ms(300)
except Exception: sensor=None
poll=select.poll();poll.register(sys.stdin,select.POLLIN)
buf='';overflow=False;temperature=None;sensor_error=None
alarm=False;sounding=False;test_until=ticks_ms();last_sample=ticks_add(ticks_ms(),-1000)
last_emit=ticks_add(ticks_ms(),-1000);last_heartbeat=ticks_add(ticks_ms(),-2000)
resolution='full';mode='stopped';direction=1;speed=40;remaining=0;executed=0;phase=0;next_step=ticks_ms();stop_reason='Boot: motor disabled'
states=((1,0,0,0),(1,0,1,0),(0,0,1,0),(0,1,1,0),
        (0,1,0,0),(0,1,0,1),(0,0,0,1),(1,0,0,1))
watchdog=WDT(timeout=2000)

def stop(reason):
    global mode,stop_reason
    enable.value(0)
    for p in coils:p.value(0)
    mode='stopped';stop_reason=reason

def sample():
    global temperature,sensor_error,alarm
    try:
        if sensor is None:raise OSError('Temperature sensor unavailable; reset to retry')
        temperature=sensor.temperature();sensor_error=None
    except Exception as e:temperature=None;sensor_error=str(e)
    if temperature is None or not alert.value():alarm=True
    elif temperature>=threshold:alarm=True
    elif temperature<threshold-2:alarm=False

def led_value(now,alarm_active,moving):
    if alarm_active:return (now//125)%2
    if moving:return (now//500)%2
    return 0

def emit(data):print(json.dumps(data))
def status():
    return {'type':'telemetry','protocol':3,'uptime_ms':ticks_ms(),'temperature_c':temperature,
      'device_name':device_name,'usb_name':device_config.usb_name,
      'usb_name_supported':device_config.usb_name is not None,'usb_name_error':device_config.usb_name_error,
      'device_id':machine.unique_id().hex(),'restarting':reset_at is not None,
      'led_on':bool(led.value()),'led_mode':'alarm' if alarm else 'running' if mode!='stopped' else 'off',
      'threshold_c':threshold,'alarm_active':alarm,'buzzer_on':sounding,'sensor_error':sensor_error,
      'thermal_alert':not bool(alert.value()),'driver_fault_asserted':not bool(fault.value()),
      'step_resolution':resolution,'steps_per_revolution':400 if resolution=='half' else 200,
      'supported_resolutions':['full','half'],'motor_enabled':bool(enable.value()),'motion_mode':mode,'direction':direction,'speed_sps':speed,
      'steps_remaining':remaining if mode=='steps' else None,'steps_executed':executed,'stop_reason':stop_reason,
      'current_a':None,'current_available':False,'board':'RP2040 / DRV8847'}

def number(value,low,high):
    if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value) or not low<=value<=high:
        raise ValueError('Value outside allowed range')
    return value

def handle(line):
    global threshold,test_until,last_heartbeat,mode,direction,speed,remaining,executed,next_step,stop_reason,resolution,device_name,reset_at
    ident=None
    try:
        cmd=json.loads(line)
        if not isinstance(cmd,dict):raise ValueError('Expected JSON object')
        ident=cmd.get('id');op=cmd.get('cmd')
        if op=='heartbeat':
            last_heartbeat=ticks_ms()
            return
        if op=='stop':stop('Stopped by user')
        elif op=='status':emit(status())
        elif op=='set_threshold':
            if mode!='stopped':raise ValueError('Stop motor before saving settings')
            value=number(cmd.get('threshold_c'),10,74)
            with open('alarm.tmp','w') as f:json.dump({'threshold_c':float(value)},f)
            os.rename('alarm.tmp','alarm.json');threshold=float(value)
        elif op=='set_device_name':
            if mode!='stopped':raise ValueError('Stop motor before renaming')
            if device_config.usb_name is None:raise ValueError('USB naming unavailable; install boot.py and device_config.py, then reset')
            if reset_at is not None:raise ValueError('Restart already pending')
            device_name=device_config.save_name(cmd.get('name'))
            stop('Restarting to apply USB name')
            reset_at=ticks_add(ticks_ms(),750)
        elif op=='beep':test_until=ticks_add(ticks_ms(),600)
        elif op=='start':
            if reset_at is not None:raise ValueError('Restart pending')
            if mode!='stopped':raise ValueError('Stop current movement first')
            new_resolution=cmd.get('resolution','full')
            if new_resolution not in ('full','half'):raise ValueError('Only full and half steps supported; quarter stepping requires controlled intermediate coil currents')
            requested=cmd.get('mode')
            if requested not in ('steps','continuous'):raise ValueError('Unknown motion mode')
            new_direction=cmd.get('direction')
            if isinstance(new_direction,bool) or new_direction not in (-1,1):raise ValueError('Direction must be -1 or 1')
            new_speed=number(cmd.get('speed_sps'),5,100)
            count=cmd.get('steps') if requested=='steps' else 0
            if requested=='steps':
                number(count,1,100000)
                if int(count)!=count:raise ValueError('Steps must be an integer')
            if ticks_diff(ticks_ms(),last_heartbeat)>1000:raise ValueError('Waiting for browser heartbeat')
            sample()
            if temperature is None or temperature>=60 or not alert.value():raise ValueError('Temperature must be valid and below 60 C')
            for p in coils:p.value(0)
            enable.value(1);sleep_ms(3)
            if not fault.value() or not alert.value():
                stop('Driver or thermal fault at wake');raise ValueError(stop_reason)
            resolution=new_resolution;direction=new_direction;speed=new_speed;remaining=int(count);executed=0
            mode=requested;stop_reason='';next_step=ticks_ms()
        else:raise ValueError('Unknown command')
        emit({'type':'ack','id':ident,'ok':True,'threshold_c':threshold,'device_name':device_name,'restarting':reset_at is not None})
    except Exception as e:
        emit({'type':'ack','id':ident,'ok':False,'error':str(e)})

try:
    while True:
        now=ticks_ms()
        if ticks_diff(now,last_sample)>=100:
            sample();last_sample=now
        if mode!='stopped':
            if temperature is None:stop('Temperature sensor error')
            elif temperature>=75 or not alert.value():stop('Thermal shutdown')
            elif not fault.value():stop('Driver fault')
            elif ticks_diff(now,last_heartbeat)>1500:stop('Browser heartbeat lost')
        for _ in range(128):
            if not poll.poll(0):break
            ch=sys.stdin.read(1)
            if ch in ('\n','\r'):
                if buf and not overflow:handle(buf)
                buf='';overflow=False
            elif not overflow:
                buf+=ch
                if len(buf)>512:buf='';overflow=True
        now=ticks_ms()
        if mode!='stopped' and ticks_diff(now,next_step)>=0:
            if mode=='steps' and remaining==0:stop('Step move complete')
            else:
                phase=(phase+direction*(1 if resolution=='half' else 2))%8
                for p in coils:p.value(0)
                for p,v in zip(coils,states[phase]):p.value(v)
                executed+=1
                if mode=='steps':remaining-=1
                next_step=ticks_add(now,round(1000/speed))
        sounding=(alarm and now%1000<200) or ticks_diff(test_until,now)>0
        buzzer.duty_u16(32768 if sounding else 0)
        led.value(led_value(now,alarm,mode!='stopped'))
        watchdog.feed()
        if reset_at is not None and ticks_diff(now,reset_at)>=0:
            stop('Restarting to apply USB name');buzzer.duty_u16(0);led.value(0)
            machine.reset()
        if ticks_diff(now,last_emit)>=250:
            emit(status());last_emit=now
        sleep_ms(1)
finally:
    stop('Firmware stopped')
    led.value(0)
    buzzer.duty_u16(0);buzzer.deinit();Pin(16,Pin.OUT,value=0)
