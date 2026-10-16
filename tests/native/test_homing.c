#define MOTOR_HOST_TEST
#define main firmware_entry
#include "../../firmware-c/main.c"
#undef main
#include <assert.h>
#include "fake_application.h"
static const char *begin(void) {
 cJSON *c=cJSON_Parse("{\"speed_full_sps\":100,\"max_travel_full_steps\":1000,\"backoff_full_steps\":100}");
 const char *error=homing_start(c);cJSON_Delete(c);return error;
}
int main(void) {
 safe_gpio();sensor_initialized=true;sample();strcpy(device_id,"e66568714f117e21");
 pd_voltage=9;power_at=0;heartbeat_seen=true;heartbeat_us=fake_us;
 /* Both production controllers and another unit use the same board-family model. */
 const char *ids[]={"e66568714f538921","e66568714f117e21","0123456789abcdef"};
 for(unsigned i=0;i<3;i++){
  strcpy(device_id,ids[i]);assert(!begin());assert(home.active&&home.phase==1);
  stop("Identity gate test complete");
 }
 assert(!begin());assert(home.active&&home.phase==1&&fake_motor.direction==-1);
 homing_tick(fake_us,true,true,100);fake_us+=100001;homing_tick(fake_us,true,true,100);
 assert(home.phase==2&&fake_motor.direction==1);
 homing_tick(fake_us,false,true,100);motor_stop();fake_motor.position_microsteps=0;
 homing_tick(fake_us,false,false,100);assert(home.phase==3);
 fake_motor.position_microsteps=8000;homing_tick(fake_us,true,true,100);
 fake_us+=100001;homing_tick(fake_us,true,true,100);assert(home.phase==4&&home.upper==6000);
 homing_tick(fake_us,false,true,100);motor_stop();fake_motor.position_microsteps=home.upper;
 homing_tick(fake_us,false,false,100);assert(home.phase==5&&fake_limits_enabled);
 motor_stop();fake_motor.position_microsteps=0;homing_tick(fake_us,false,false,100);
 assert(!home.active&&homed&&!needs_homing&&settings.bounds_saved&&settings.travel_max==6000);
 assert(fake_limit_max==6000);
 heartbeat_us=fake_us;assert(!begin());stop("Stopped by user");
 assert(!home.active&&!homed&&needs_homing&&settings.travel_max==6000);
 heartbeat_us=fake_us;assert(!begin());motor_stop();homing_tick(fake_us,false,false,100);
 assert(!home.active&&strstr(home.message,"No end stop"));
 heartbeat_us=fake_us;assert(!begin());homing_tick(home.deadline,false,false,100);
 assert(!home.active&&strstr(home.message,"timed out"));
 heartbeat_us=fake_us;assert(!begin());homing_tick(fake_us,true,true,100);fake_us+=100001;
 homing_tick(fake_us,true,true,100);motor_stop();homing_tick(fake_us,false,false,100);
 assert(!home.active&&strstr(home.message,"did not clear"));
 puts("Homing: two contacts, clearance, conservative span, zero return, persistence, cancellation and failures passed");
}
void current_capture_init(void) {}
bool current_capture_snapshot(uint16_t raw[2]) {raw[0]=2048;raw[1]=2048;return true;}
void current_capture_read(current_frame_t *f, bool connected) {memset(f,0,sizeof(*f));}
void current_capture_drop(unsigned bins) {}
