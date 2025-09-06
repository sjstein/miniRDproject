import argparse
import json
import run8
import serial
import serial.tools.list_ports
import socket
import time

run8port = 7766
local_ip = '127.0.0.1'
cal_fname = 'miniRD.cal'

indy_deadband = 1
auto_deadband = 1
dyn_deadband = 1
throttle_delta = 10
rev_deadband = 1

button_up = 0
button_down = 1
off = 0
on = 1
counter_up = 1
counter_release = 0
counter_down = 2
r8max_val = 255

toggle_back = 50
toggle_forward = 180

alerter_time = 30

def crc(blist):
    res = 0
    for b in blist:
        res = res ^ b
    return res

def form_msg(typ, cmd, data):
    msg_arr = bytes([typ, 0, cmd, data, crc([typ, cmd, data])])
    return msg_arr

def update_state(out_sock, index, value, quiet=False, v_lvl=0):
    if v_lvl > 1:
        print(f'{run8.cmd_dict[run8.cmd_list[index]]} {value}')
    if quiet:
        out_sock.sendto(form_msg(run8.header_quiet, run8.cmd_list[index], int(value)), (local_ip, run8port))
    else:
        out_sock.sendto(form_msg(run8.header_sound, run8.cmd_list[index], int(value)), (local_ip, run8port))

def update_raw_state(out_sock, index, value, quiet=False):
    if quiet:
        out_sock.sendto(form_msg(run8.header_quiet, index, int(value)), (local_ip, run8port))
    else:
        out_sock.sendto(form_msg(run8.header_sound, index, int(value)), (local_ip, run8port))

def alt_key_pressed(msg):
    if msg[run8.cmd_list.index(run8.cmd_alerter)] == 0:
        return False
    else:
        return True

def find_com_ports():
    com_ports = []
    for port in serial.tools.list_ports.comports():
        com_ports.insert(0, port.device)
    return com_ports

def scale(lever, value, calibration):
    lval = int(calibration[lever]['min'])
    hval = int(calibration[lever]['max'])
    scaled_val = int(((value - lval) * r8max_val)/(hval - lval))
    if scaled_val < 0:
        scaled_val = 0
    elif scaled_val > r8max_val:
        scaled_val = 255
    return scaled_val

def calibrate_throttle(port, notch : int):
    input(f'[{time.strftime("%H:%M:%S", time.localtime())}] '
          f'--> Move throttle to notch {notch} and press return')
    print(f'[{time.strftime("%H:%M:%S", time.localtime())}] <-- Reading throttle')
    time.sleep(1)
    port.write(b'r\n')
    in_line = port.readline().decode('utf-8')
    current_message = list(map(int, in_line.split(',')))
    print(f'[{time.strftime("%H:%M:%S", time.localtime())}] Notch {notch} rval: {current_message[3]}')
    return int(current_message[3])

def main():
    try:
        fp = open(cal_fname, 'r')
    except FileNotFoundError:
        print('Calibration file not found - creating default')
        calib_data = {'auto': {'min': 0, 'max': 1023}, 'indy': {'min': 0, 'max': 1023},
                      'dyn': {'min': 0, 'max': 1023}, 'thr': {'min': 0, 'max': 1023}, 'rev': {'min': 0, 'max': 10230}}
        fp = open(cal_fname, 'w')
        json_object = json.dumps(calib_data, indent=4)
        fp.write(json_object)
        fp.close()
        fp = open(cal_fname, 'r')
    calib_data = json.load(fp)

    if 'thr0' not in calib_data:
        # Use the coarse throttle min/max to create 9 even bins
        base_min = int(calib_data.get('thr', {}).get('min', 0))
        base_max = int(calib_data.get('thr', {}).get('max', 1023))
        span = max(1, base_max - base_min)
        for j in range(9):
            lo = base_min + int(round(j * span / 9.0))
            hi = base_min + int(round((j + 1) * span / 9.0)) - 1
            if hi < lo:
                hi = lo
            calib_data[f'thr{j}'] = {'min': lo, 'max': hi}

        # Persist the upgraded calibration so future runs are fine
        with open(cal_fname, 'w') as _out:
            json.dump(calib_data, _out, indent=4)
        print(f'Upgraded calibration file with thr0..thr8 bins and saved to {cal_fname}')



    previous_indy = 255
    previous_auto = 0
    previous_dyn = 0
    previous_throttle = 0
    previous_reverser = 0
    previous_counter = 0
    previous_throttle_val = 0
    previous_reverser_val = 0
    requested_notch = 0
    previous_notch = 0

    wiper_value = 0
    sand_value = 0
    slow_speed_value = 0
    gauge_light_value = 0
    cab_light_value = 0

    # Handbrake state: 0 = last was set, 1 = last was rel
    handbrake_toggle = 0

    auto_alerter = False
    alerter_pressed = False
    perform_cal = False

    parser = argparse.ArgumentParser(description='Python script to serve as miniRD daemon',
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-p', '--port', help='Serial (COM) port the MiniRD is connected to (optional). \n'
                                             'If left blank, this tool will poll all available serial ports.',
                        default=None, type=str)
    parser.add_argument('-v', '--verbosity', help=f'Verbosity level 0 (silent) to 3 (most verbose).',
                        type=int, default=3)
    args = parser.parse_args()
    valid_port = args.port
    verbosity = args.verbosity

    if not valid_port:
        ports = find_com_ports()
        if ports:
            if verbosity > 0:
                print("Available COM ports: {ports}".format(ports=ports))
            for port in ports:
                trying_port = True
                if verbosity > 0:
                    print(f"trying {port}:")
                try:
                    t_port = serial.Serial(port=port, baudrate=9600, timeout=1, write_timeout=1)
                    time.sleep(2)
                    t_port.write(b'I\r\n')
                except serial.SerialException:
                    if verbosity > 0:
                        print('Port unreachable')
                    trying_port = False
                if trying_port:
                    try:
                        t_port.write(b'I\r\n')
                    except serial.SerialTimeoutException:
                        if verbosity > 0:
                            print('Timeout')
                        trying_port = False
                if trying_port:
                    in_line = t_port.readline().strip().decode('utf-8').split(',')
                    if in_line[0] == 'miniRD':
                        if verbosity > 0:
                            print('----------------------------')
                            print(f'Found miniRD on {port}, firmware version: {in_line[1]}')
                        t_port.close()
                        valid_port = port
                        break
                    else:
                        if verbosity > 0:
                            print(f'Valid port found at {port}, but no miniRD responding.')
                        t_port.close()
                        time.sleep(2)
            if not valid_port:
                if verbosity > 0:
                    print('No miniRD found on any COM port.')
                exit(0)
        else:
            if verbosity > 0:
                print("No COM ports found.")
            exit(0)


    # Open UDP socket
    out_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Open serial port to communicate to miniRD
    s_port = serial.Serial(port=valid_port, baudrate=9600, timeout=5)
    time.sleep(2)   # delay a bit to allow port to settle
    if verbosity > 0:
        print(f'MiniRD server started at {time.strftime("%H:%M:%S", time.localtime())}')
        print(f'UDP stream to {local_ip}:{run8port}')

    
    s_port.write(b'r\n')
    start_time = time.time()
    while True:
        in_line = s_port.readline().decode('utf-8').strip()
        if in_line:  # Non-blank
            print("Received values:", in_line)
            print("Number of fields received:", len(in_line.split(',')))
            break
        elif time.time() - start_time > 1.0:
            print("No data received from Arduino after 1 second. Exiting.")
            exit(1)
        # else: keep looping until timeout
    last_message = list(map(int, in_line.split(',')))
    previous_time = time.time()

    while True:
        if perform_cal:
            print(f'--------------------\n[{time.strftime("%H:%M:%S", time.localtime())}] '
                  f'MiniRD Recalibration requested\n--------------------\n')
            resp = input(f'What type of calibration: (b)rake levers, (t)hrottle notches, (a)ll, or (c)ancel? ')
            cal_brakes = True
            cal_throttle = True
            if resp.lower() == 'c':
                print(f'Calibration cancelled')
                cal_throttle = False
                cal_brakes = False
            if resp.lower() == 'b':
                cal_throttle = False
            if resp.lower() == 't':
                cal_brakes = False

            if cal_brakes:
                input(f'[{time.strftime("%H:%M:%S", time.localtime())}] '
                      f'--> Move all levers (except throttle) to one extreme and press return')
                print(f'[{time.strftime("%H:%M:%S", time.localtime())}] <-- Reading current lever values')
                time.sleep(1)
                s_port.write(b'r\n')
                in_line = s_port.readline().decode('utf-8')
                current_message = list(map(int, in_line.split(',')))
                auto_v1 = int(current_message[0])
                indy_v1 = int(current_message[1])
                dyn_v1 = int(current_message[2])
                input(f'[{time.strftime("%H:%M:%S", time.localtime())}] '
                      f'--> Move all levers (except throttle) to their other extremes and press return')
                print(f'[{time.strftime("%H:%M:%S", time.localtime())}] <-- Reading current lever values')
                time.sleep(1)
                s_port.write(b'r\n')
                in_line = s_port.readline().decode('utf-8')
                current_message = list(map(int, in_line.split(',')))
                auto_v2 = int(current_message[0])
                indy_v2 = int(current_message[1])
                dyn_v2 = int(current_message[2])
                # Update calibration structure
                calib_data['auto']['min'] = min(auto_v1, auto_v2)
                calib_data['auto']['max'] = max(auto_v1, auto_v2)
                calib_data['indy']['min'] = min(indy_v1, indy_v2)
                calib_data['indy']['max'] = max(indy_v1, indy_v2)
                calib_data['dyn']['min'] = min(dyn_v1, dyn_v2)
                calib_data['dyn']['max'] = max(dyn_v1, dyn_v2)

            if cal_throttle:
                input(f'[{time.strftime("%H:%M:%S", time.localtime())}] '
                      f'--> Move throttle up to notch 2 and press return')
                thr_n_up = []  # Moving up the notches
                for i in range(9):
                    thr_n_up.append(calibrate_throttle(s_port, i))
                input(f'[{time.strftime("%H:%M:%S", time.localtime())}] '
                      f'--> Move throttle down to notch 6 and press return')
                thr_n_dwn = []  # Moving down the notches
                for i in range(8, -1, -1):
                    thr_n_dwn.append(calibrate_throttle(s_port, i))
                for i in range(9):
                    calib_data[f'thr{i}']['min'] = min(thr_n_up[i], thr_n_dwn[8 - i])
                    calib_data[f'thr{i}']['max'] = max(thr_n_up[i], thr_n_dwn[8 - i])

            if cal_throttle or cal_brakes:
                print(f'--------------------\n[{time.strftime("%H:%M:%S", time.localtime())}] '
                      f'MiniRD Recalibration completed\n--------------------')
                print(f'New calibration: {calib_data}')
                fp = open(cal_fname, 'w')
                json_object = json.dumps(calib_data, indent=4)
                fp.write(json_object)
                fp.close()
                print(f'----------\nNew Calibration data saved to {cal_fname}\nRestarting daemon\n------------')
            perform_cal = False

        s_port.write(b'r\r\n')
        in_line = s_port.readline().decode('utf-8')
        current_message = list(map(int, in_line.split(',')))

        if auto_alerter:
            if time.time() - previous_time > alerter_time:
                if time.time() - previous_time > alerter_time + .1:
                    alerter_pressed = False
                    previous_time = time.time()
                    update_raw_state(out_sock, run8.cmd_alerter, off, quiet=True)
                elif not alerter_pressed:
                    alerter_pressed = True
                    update_raw_state(out_sock, run8.cmd_alerter, on, quiet=True)

        for i in range(len(current_message)):
            if current_message[i] != last_message[i]:
                last_message[i] = current_message[i]

                if run8.cmd_list[i] == run8.cmd_throttle:
                    throttle_val = current_message[i]
                    if verbosity > 2:
                        print(f'Throttle rval: {throttle_val}')
                    for j in range(9):
                        if ((calib_data[f'thr{j}']['min'] - throttle_delta) < throttle_val
                            < (calib_data[f'thr{j}']['max'] + throttle_delta)):
                            requested_notch = j
                            break
                    if requested_notch != previous_notch:
                        previous_notch = requested_notch
                        # print(f'Throttle update: {previous_notch}')
                        update_state(out_sock, i, previous_notch, v_lvl=verbosity)

                elif run8.cmd_list[i] == run8.cmd_indy_brake:
                    requested_indy = scale('indy', int(current_message[i]), calib_data)
                    if abs(previous_indy - requested_indy) > indy_deadband:
                        previous_indy = requested_indy
                        if previous_indy < 0:
                            previous_indy = 0
                        update_state(out_sock, i, previous_indy, v_lvl=verbosity)

                elif run8.cmd_list[i] == run8.cmd_auto_brake:
                    requested_auto = scale('auto', int(current_message[i]), calib_data)
                    if abs(previous_auto - requested_auto) > auto_deadband:
                        previous_auto = requested_auto
                        if previous_auto <= auto_deadband:
                            previous_auto = 0
                        update_state(out_sock, i, previous_auto, v_lvl=verbosity)

                elif run8.cmd_list[i] == run8.cmd_dyn_brake:
                    requested_dyn = scale('dyn', int(current_message[i]), calib_data)
                    if abs(previous_dyn - requested_dyn) > auto_deadband:
                        previous_dyn = requested_dyn
                        if previous_dyn < 3:
                            previous_dyn = 0
                        update_state(out_sock, i, previous_dyn, v_lvl=verbosity)

                elif run8.cmd_list[i] == run8.cmd_reverser:
                    requested_rev = scale('rev', current_message[i], calib_data)
                    if abs(previous_reverser - requested_rev) > rev_deadband:
                        if (256//3) * 2 <= requested_rev <= (256//3) * 3:
                            update_state(out_sock, i, run8.reverser_forward, v_lvl=verbosity)
                            previous_reverser = requested_rev
                        elif (256//3) * 1 < requested_rev < (256//3) * 2:
                            update_state(out_sock, i, run8.reverser_neutral, v_lvl=verbosity)
                            previous_reverser = requested_rev
                        else:
                            update_state(out_sock, i, run8.reverser_reverse, v_lvl=verbosity)
                            previous_reverser = requested_rev

                elif run8.cmd_list[i] == run8.cmd_counter:
                    update_state(out_sock, i, current_message[i], v_lvl=verbosity)  

                elif run8.cmd_list[i] == run8.cmd_dpu_fence_inc:
                    # Value: 1=inc, 2=dec, 0=none
                    if current_message[i] == 1:
                        update_raw_state(out_sock, run8.cmd_dpu_fence_inc, 1)
                        update_raw_state(out_sock, run8.cmd_dpu_fence_dec, 0)
                    elif current_message[i] == 2:
                        update_raw_state(out_sock, run8.cmd_dpu_fence_inc, 0)
                        update_raw_state(out_sock, run8.cmd_dpu_fence_dec, 1)
                    else:
                        update_raw_state(out_sock, run8.cmd_dpu_fence_inc, 0)
                        update_raw_state(out_sock, run8.cmd_dpu_fence_dec, 0)

                elif run8.cmd_list[i] == run8.cmd_dpu_thr_inc:
                    if current_message[i] == 1:
                        update_raw_state(out_sock, run8.cmd_dpu_thr_inc, 1)
                        update_raw_state(out_sock, run8.cmd_dpu_thr_dec, 0)
                    elif current_message[i] == 2:
                        update_raw_state(out_sock, run8.cmd_dpu_thr_inc, 0)
                        update_raw_state(out_sock, run8.cmd_dpu_thr_dec, 1)
                    else:
                        update_raw_state(out_sock, run8.cmd_dpu_thr_inc, 0)
                        update_raw_state(out_sock, run8.cmd_dpu_thr_dec, 0)

                elif run8.cmd_list[i] == run8.cmd_dpu_dyn_setup:
                    update_state(out_sock, i, current_message[i], v_lvl=verbosity)

                elif run8.cmd_list[i] == run8.cmd_slow_speed_toggle and current_message[i] == button_down:
                    slow_speed_value += 1
                    if slow_speed_value > 1:
                        slow_speed_value = 0
                    update_state(out_sock, i, slow_speed_value, v_lvl=verbosity)

                elif run8.cmd_list[i] == run8.cmd_park_brake_set and current_message[i] == 1:
                    if handbrake_toggle == 0:
                        update_raw_state(out_sock, run8.cmd_park_brake_set, 1)
                        update_raw_state(out_sock, run8.cmd_park_brake_rel, 0)
                        handbrake_toggle = 1
                    else:
                        update_raw_state(out_sock, run8.cmd_park_brake_set, 0)
                        update_raw_state(out_sock, run8.cmd_park_brake_rel, 1)
                        handbrake_toggle = 0

                elif run8.cmd_list[i] == run8.cmd_wiper and current_message[i] == button_down:    
                    wiper_value += 1
                    if wiper_value > 3:
                        wiper_value = 0
                    update_state(out_sock, i, wiper_value, v_lvl=verbosity)
                #elif run8.cmd_list[i] == run8.cmd_sand and current_message[i] == button_down:
                #    sand_value = int(not sand_value)
                #    update_state(out_sock, i, sand_value, v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_sand:
                    update_state(out_sock, i, sand_value, v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_bell:
                    update_state(out_sock, i, current_message[i], v_lvl=verbosity)    
                elif run8.cmd_list[i] == run8.cmd_alerter:
                    update_state(out_sock, i, current_message[i], v_lvl=verbosity)    
                elif run8.cmd_list[i] == run8.cmd_gauge_light and current_message[i] == button_down:
                    gauge_light_value += 1
                    if gauge_light_value > 1:
                        gauge_light_value = 0
                    update_raw_state(out_sock, run8.cmd_step_light, gauge_light_value)
                    update_state(out_sock, i, gauge_light_value, v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_cab_light and current_message[i] == button_down:
                    cab_light_value += 1
                    if cab_light_value > 1:
                        cab_light_value = 0
                    update_state(out_sock, i, cab_light_value, v_lvl=verbosity)    
                elif run8.cmd_list[i] == run8.cmd_cktbrk_engrun:
                    update_state(out_sock, i, current_message[i], v_lvl=verbosity)    
                elif run8.cmd_list[i] == run8.cmd_cktbrk_genfld:
                    update_state(out_sock, i, current_message[i], v_lvl=verbosity) 
                elif run8.cmd_list[i] == run8.cmd_cktbrk_ctl:
                    update_state(out_sock, i, current_message[i], v_lvl=verbosity) 
                elif run8.cmd_list[i] == run8.cmd_bail:
                    if alt_key_pressed(current_message):
                        if not bool(current_message[i]):
                            auto_alerter = not auto_alerter
                            if verbosity > 1:
                                print(f'auto_alerter : {auto_alerter}')
                    else:
                        update_state(out_sock, i, current_message[i], v_lvl=verbosity)    
                elif run8.cmd_list[i] == run8.cmd_horn:
                    if alt_key_pressed(current_message):
                        perform_cal = True
                        pass
                    else:
                        update_state(out_sock, i, current_message[i], v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_headlight_front:
                    update_state(out_sock, i, current_message[i], v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_headlight_rear:
                    update_state(out_sock, i, current_message[i], v_lvl=verbosity)
                else:
                    pass

if __name__ == "__main__":
    main()
