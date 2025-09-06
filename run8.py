# Constants and data structures specific to the run8 interface


# Run8 defined commands
# These define the command number as outlined in the Run8 API documentation

cmd_alerter = 1
cmd_bell = 2
cmd_counter = 3
cmd_dyn_brake = 4
cmd_headlight_front = 5
cmd_headlight_rear = 6
cmd_mu_headlight = 7
cmd_horn = 8
cmd_indy_brake = 9
cmd_bail = 10
cmd_iso_switch = 11
cmd_park_brake_set = 12
cmd_park_brake_rel = 13
cmd_reverser = 14
cmd_sand = 15
cmd_throttle = 16
cmd_t_motors = 17
cmd_auto_brake = 18
cmd_wiper = 19
cmd_dtmf_0 = 20
cmd_dtmf_1 = 21
cmd_dtmf_2 = 22
cmd_dtmf_3 = 23
cmd_dtmf_4 = 24
cmd_dtmf_5 = 25
cmd_dtmf_6 = 26
cmd_dtmf_7 = 27
cmd_dtmf_8 = 28
cmd_dtmf_9 = 29
cmd_dtmf_p = 30
cmd_dtmf_s = 31
cmd_radio_vol_up = 32
cmd_radio_vol_dwn = 33
cmd_radio_mute = 34
cmd_radio_ch_mode = 35
cmd_radio_dtmf_mode = 36
cmd_cktbrk_ctl = 37
cmd_cktbrk_dbrake = 38
cmd_cktbrk_engrun = 39
cmd_cktbrk_genfld = 40
cmd_cab_light = 41
cmd_step_light = 42
cmd_gauge_light = 43
cmd_emerg_stop = 44
cmd_auto_start = 45
cmd_auto_mu = 46
cmd_auto_cb = 47
cmd_auto_ab = 48
cmd_auto_eot = 49
cmd_eng_start = 50
cmd_eng_stop = 51
cmd_hep_switch = 52
cmd_tbrake_cutoff = 53
cmd_service_sel = 54
cmd_slow_speed_toggle = 55
cmd_slow_speed_inc = 56
cmd_slow_speed_dec = 57
cmd_dpu_thr_inc = 58  # DPU Throttle Increase
cmd_dpu_thr_dec = 59  # DPU Throttle Decrease
cmd_dpu_dyn_setup = 60  # DPU Dyn‑Brake Setup
cmd_dpu_fence_inc = 61  # DPU Fence Increase
cmd_dpu_fence_dec = 62  # DPU Fence Decrease
cmd_on = 1
cmd_off = 0

header_quiet = 96  # This message header tells Run8 not to play sounds in cab
header_sound = 224  # This message header tells Run8 to play sound in cab

reverser_forward = 255
reverser_neutral = 127
reverser_reverse = 0

cmd_list = [cmd_auto_brake, cmd_indy_brake, cmd_dyn_brake, cmd_throttle, cmd_reverser, cmd_counter,
            cmd_dpu_fence_inc, cmd_dpu_thr_inc, cmd_dpu_dyn_setup, cmd_slow_speed_toggle, cmd_park_brake_set, 
            cmd_wiper, cmd_sand, cmd_bell, cmd_alerter, cmd_gauge_light, cmd_cab_light, cmd_cktbrk_engrun, 
            cmd_cktbrk_genfld, cmd_cktbrk_ctl, cmd_bail, cmd_horn, cmd_headlight_front, cmd_headlight_rear]

cmd_dict = {cmd_auto_brake: 'auto_brake', cmd_indy_brake: 'indy_brake', cmd_dyn_brake: 'dyn_brake',
            cmd_throttle: 'throttle', cmd_reverser: 'reverser', cmd_counter: 'counter', cmd_dpu_fence_inc: 'dpu_fence_inc',
            cmd_dpu_thr_inc: 'dpu_thr_inc', cmd_dpu_dyn_setup: 'DPU_dyn_setup', cmd_slow_speed_toggle: 'slow_speed_toggle',
            cmd_park_brake_set: 'park_brake', cmd_wiper: 'wiper',  cmd_sand: 'sand', cmd_bell: 'bell', cmd_alerter: 'alerter',
            cmd_gauge_light: 'gauge/step light', cmd_cab_light: 'cab light', cmd_cktbrk_engrun: 'eng_run',
            cmd_cktbrk_genfld: 'gen_field', cmd_cktbrk_ctl: 'control', cmd_bail: 'bail', cmd_horn: 'horn',
            cmd_headlight_front: 'headlight_front', cmd_headlight_rear: 'headlight_rear'}


