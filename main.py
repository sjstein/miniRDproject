import argparse
import json
import run8
import serial
import serial.tools.list_ports
import socket
import time

# Network/comms info
run8port = 7766  # Must match Run 8 value in UI
local_ip = '127.0.0.1'  # Run 8 only listens on local loopback interface

# File info
cal_fname = 'miniRD.cal'

# Deadband values to minimize propagating noise
indy_deadband = 1
auto_deadband = 1
dyn_deadband = 1
throttle_delta = 10  # Range band to indicate notches

# "Constants" for readability
button_up = 0
button_down = 1
off = 0
on = 1
reverser_forward = 1
reverser_neutral = 2
reverser_reverse = 0
counter_up = 1
counter_release = 0
counter_down = 2
r8max_val = 255  # Highest number run8 will accept in UDP stream

# The miniRD uses an ADC input for toggle switches with resistor voltage divider; these values define breakpoints
toggle_back = 50
toggle_forward = 180

# Timing values
alerter_time = 30  # Seconds between auto-alerter virtual press


def crc(blist):
    # Generate CRC byte
    res = 0
    for b in blist:
        res = res ^ b
    return res


def form_msg(typ, cmd, data):
    # Form the message byte array
    msg_arr = bytes([typ, 0, cmd, data, crc([typ, cmd, data])])
    return msg_arr


def update_state(out_sock, index, value, quiet=False, v_lvl=0):
    # Send the update message using the run8.cmd_list array
    if v_lvl > 1:
        print(f'{run8.cmd_dict[run8.cmd_list[index]]} {value}')
    if quiet:
        out_sock.sendto(form_msg(run8.header_quiet, run8.cmd_list[index], int(value)), (local_ip, run8port))
    else:
        out_sock.sendto(form_msg(run8.header_sound, run8.cmd_list[index], int(value)), (local_ip, run8port))


def update_raw_state(out_sock, index, value, quiet=False):
    # Send the update message using a raw command value
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
    try:
        scaled_val = int(r8max_val * ((value - lval) / (hval - lval)))

    except ZeroDivisionError:
        print('It appears your calibration file is corrupt. Please delete the file miniRD.cal and re-run the daemon')
        exit(-1)

    if scaled_val < 0:
        # print(f'** Warning ** {lever} value out of bounds ({scaled_val}), consider recalibrating')
        scaled_val = 0
    elif scaled_val > r8max_val:
        # print(f'** Warning ** {lever} value out of bounds ({scaled_val}), consider recalibrating')
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
    return int(current_message[3])


def main():
    # Open calibration file and load data
    try:
        fp = open(cal_fname, 'r')
    except FileNotFoundError:
        print('Calibration file not found - creating default')
        calib_data = {'auto': {'min': 0, 'max': 1023}, 'indy': {'min': 0, 'max': 1023},
                      'dyn': {'min': 0, 'max': 1023}, 'thr0': 0, 'thr1': 114, 'thr2': 228, 'thr3' : 342,
                      'thr4': 446, 'thr5': 560, 'thr6': 674, 'thr7': 788, 'thr8': 902}
        fp = open(cal_fname, 'w')
        json_object = json.dumps(calib_data, indent=4)
        fp.write(json_object)
        fp.close()
        fp = open(cal_fname, 'r')
    calib_data = json.load(fp)

    # Keep track of lever positions
    previous_indy = 255
    previous_auto = 0
    previous_dyn = 0
    previous_throttle = 0
    previous_reverser = 2
    previous_counter = 0
    previous_throttle_val = 0
    requested_notch = 0
    previous_notch = 0

    # Keep track of latching / multivalued buttons
    wiper_value = 0
    sand_value = 0
    slow_speed_value = 0
    front_headlight_value = 0
    rear_headlight_value = 0
    step_light_value = 0
    gauge_light_value = 0
    cab_light_value = 0

    # Used for periodically pressing alerter button so player can leave the cab
    auto_alerter = False
    alerter_pressed = False

    perform_cal = False

    # Parse command-line arguments
    parser = argparse.ArgumentParser(description='Python script to test com port',
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-p', '--port', help='Serial (COM) port test', default=None, type=str)
    parser.add_argument('-v', '--verbosity', help=f'Verbosity level 0 (silent) to 3 (most verbose).',
                        type=int, default=0)
    args = parser.parse_args()
    valid_port = args.port
    verbosity = args.verbosity

    # Find valid COM port
    if not valid_port:
        ports = find_com_ports()
        if ports:
            if verbosity > 0:
                print("Dev build 01a")
                print("Available COM ports: {ports}".format(ports=ports))
            for port in ports:
                trying_port = True
                if verbosity > 0:
                    print(f"trying {port}:")
                try:
                    t_port = serial.Serial(port=port, baudrate=9600, timeout=1)
                    time.sleep(2)   # Delay a bit to allow port to open
                except serial.SerialException:
                    if verbosity > 0:
                        print('Port unreachable')
                    trying_port = False

                if trying_port:
                    try:
                        t_port.write(b'I\n')  # noqa
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

    # prime the pump - populate the last_messsage list
    s_port.write(b'r\n')
    in_line = s_port.readline().decode('utf-8')
    last_message = list(map(int, in_line.split(',')))
    previous_time = time.time()

    # message structure for reading values from the controller:
    # <auto_brake>, <indy_brake>, <dynamic_brake>, <throttle> (values between 0-255)
    # <reverser>, <counter> (values between 0 and 2)
    # <pb0>, <pb_1>, <pb_2>, <pb_3>, <pb_4>, <pb_5>, <pb_6>, <pb_7> (values 0 or 1)

    while True:
        if perform_cal:
            print(f'--------------------\n[{time.strftime("%H:%M:%S", time.localtime())}] '
                  f'MiniRD Recalibration requested\n--------------------\n')
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
            thr_v1 = int(current_message[3])
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
            thr_v2 = int(current_message[3])
            thr_n = []
            for i in range(9):
                thr_n.append(calibrate_throttle(s_port, i))
            print(f'--------------------\n[{time.strftime("%H:%M:%S", time.localtime())}] '
                  f'MiniRD Recalibration completed\n--------------------')

            print(f'Old calibration: {calib_data}')
            calib_data['auto']['min'] = min(auto_v1, auto_v2)
            calib_data['auto']['max'] = max(auto_v1, auto_v2)
            calib_data['indy']['min'] = min(indy_v1, indy_v2)
            calib_data['indy']['max'] = max(indy_v1, indy_v2)
            calib_data['dyn']['min'] = min(dyn_v1, dyn_v2)
            calib_data['dyn']['max'] = max(dyn_v1, dyn_v2)
            for i in range(9):
                calib_data[f'thr{i}'] = thr_n[i]
            print(f'New calibration: {calib_data}')
            fp = open(cal_fname, 'w')
            json_object = json.dumps(calib_data, indent=4)
            fp.write(json_object)
            fp.close()
            print(f'----------\nNew Calibration data saved to {cal_fname}\nRestarting daemon\n------------')
            perform_cal = False

        s_port.write(b'r\n')  # Ask arduino for a status string
        in_line = s_port.readline().decode('utf-8')  # Read status values
        current_message = list(map(int, in_line.split(',')))  # Convert to list

        # Service the auto_alerter function if necessary
        if auto_alerter:
            if time.time() - previous_time > alerter_time:
                if time.time() - previous_time > alerter_time + .1:  # Keep the alerter pressed for ~0.1s
                    alerter_pressed = False
                    previous_time = time.time()
                    update_raw_state(out_sock, run8.cmd_alerter, off, quiet=True)
                elif not alerter_pressed:
                    alerter_pressed = True
                    update_raw_state(out_sock, run8.cmd_alerter, on, quiet=True)

        # Walk through each element in status string and service those which have changed
        for i in range(len(current_message)):
            if current_message[i] != last_message[i]:
                last_message[i] = current_message[i]  # Push current status value into previous list
                if run8.cmd_list[i] == run8.cmd_throttle:
                    throttle_val = current_message[i]
                    for j in range(9):
                        if throttle_val < calib_data[f'thr{j}'] + 20:   # Best guess at a deadband
                            request_notch = j
                            break
                    if requested_notch != previous_notch:
                        previous_notch = requested_notch
                        # print(f'Throttle update: {previous_notch}')
                        update_state(out_sock, i, previous_notch, v_lvl=verbosity)
                    previous_throttle_val = throttle_val
                elif run8.cmd_list[i] == run8.cmd_indy_brake:
                    requested_indy = scale('indy', int(current_message[i]), calib_data)
                    if abs(previous_indy - requested_indy) > indy_deadband:
                        previous_indy = requested_indy
                        # "Reverse" direction of indy lever output
                        rev_indy = 255 - previous_indy
                        if rev_indy < 0:
                            rev_indy = 0
                        update_state(out_sock, i, rev_indy, v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_auto_brake:
                    requested_auto = scale('auto', int(current_message[i]), calib_data)
                    if abs(previous_auto - requested_auto) > auto_deadband:
                        previous_auto = requested_auto
                        if previous_auto <= auto_deadband:
                            previous_auto = 0  # Assure hitting the "release" value
                        update_state(out_sock, i, previous_auto, v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_dyn_brake:
                    requested_dyn = scale('dyn', int(current_message[i]), calib_data)
                    if abs(previous_dyn - requested_dyn) > auto_deadband:
                        previous_dyn = requested_dyn
                        if previous_dyn <= dyn_deadband:
                            previous_dyn = 0
                        update_state(out_sock, i, previous_dyn, v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_reverser:
                    if current_message[i] == reverser_reverse:
                        if previous_reverser != reverser_reverse:
                            update_state(out_sock, i, run8.reverser_reverse, v_lvl=verbosity)
                            previous_reverser = reverser_reverse
                    elif current_message[i] == reverser_forward:
                        if previous_reverser != reverser_forward:
                            update_state(out_sock, i, run8.reverser_forward, v_lvl=verbosity)
                            previous_reverser = reverser_forward
                    else:
                        if previous_reverser != reverser_neutral:
                            update_state(out_sock, i, run8.reverser_neutral, v_lvl=verbosity)
                            previous_reverser = reverser_neutral
                elif run8.cmd_list[i] == run8.cmd_wiper and current_message[i] == button_down:
                    if alt_key_pressed(current_message):
                        step_light_value = int(not step_light_value)
                        update_raw_state(out_sock, run8.cmd_step_light, step_light_value)
                    else:
                        wiper_value += 1
                        if wiper_value > 3:  # wiper has value in range 0,3 (inclusive)
                            wiper_value = 0
                        update_state(out_sock, i, wiper_value, v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_sand and current_message[i] == button_down:
                    if alt_key_pressed(current_message):
                        gauge_light_value = int(not gauge_light_value)
                        update_raw_state(out_sock, run8.cmd_gauge_light, gauge_light_value)
                    else:
                        sand_value = int(not sand_value)
                        update_state(out_sock, i, sand_value, v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_headlight_front and current_message[i] == button_down:
                    if alt_key_pressed(current_message):
                        cab_light_value = int(not cab_light_value)
                        update_raw_state(out_sock, run8.cmd_cab_light, cab_light_value)
                    else:
                        front_headlight_value += 1
                        if front_headlight_value > 2:  # Headlight has value 0,2 (inclusive)
                            front_headlight_value = 0
                        update_state(out_sock, i, front_headlight_value, v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_headlight_rear and current_message[i] == button_down:
                    if alt_key_pressed(current_message):
                        slow_speed_value = int(not slow_speed_value)
                        update_raw_state(out_sock, run8.cmd_slow_speed_toggle, slow_speed_value)
                    else:
                        rear_headlight_value += 1
                        if rear_headlight_value > 2:  # Headlight has value 0,2 (inclusive)
                            rear_headlight_value = 0
                        update_state(out_sock, i, rear_headlight_value, v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_counter:
                    if alt_key_pressed(current_message):
                        if current_message[i] == counter_down:
                            update_raw_state(out_sock, run8.cmd_park_brake_rel, on)
                        elif current_message[i] == counter_up:
                            update_raw_state(out_sock, run8.cmd_park_break_set, on)
                        else:
                            update_raw_state(out_sock, run8.cmd_park_break_set, off)
                            update_raw_state(out_sock, run8.cmd_park_brake_rel, off)
                    else:
                        if previous_counter != current_message[i]:
                            update_state(out_sock, i, current_message[i], v_lvl=verbosity)
                            previous_counter = current_message[i]
                elif run8.cmd_list[i] == run8.cmd_horn:
                    if alt_key_pressed(current_message):
                        perform_cal = True
                        pass
                    else:
                        update_state(out_sock, i, current_message[i], v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_bell:
                    if alt_key_pressed(current_message):
                        # No alt function defined yet
                        pass
                    else:
                        update_state(out_sock, i, current_message[i], v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_alerter:
                    update_state(out_sock, i, current_message[i], v_lvl=verbosity)
                elif run8.cmd_list[i] == run8.cmd_bail:
                    if alt_key_pressed(current_message):
                        if not bool(current_message[i]):
                            auto_alerter = not auto_alerter
                            if verbosity > 1:
                                print(f'auto_alerter : {auto_alerter}')
                    else:
                        update_state(out_sock, i, current_message[i], v_lvl=verbosity)
                else:
                    pass


if __name__ == "__main__":
    main()
