import socket
import argparse
import time


# Global variable
global_counter = 0

def receive(sock):
    """
    Receptions the data arriving at the sock Socket

    Parameters
    ----------
    sock -- socket on which we receive the data (Socket)

    Returns
    -------
    buf -- binary string with all the content received (binary str)
    """

    data = sock.recv(1)
    buf = b""

    # While there is data, keep receiving
    while data.decode("utf-8") != "\n":
        buf += data
        data = sock.recv(1)
    
    return buf

def main(ip, port):
    """
    Main loop; communication establishment
    + exchange/receive messages with ip:port

    Parameters
    ----------
    ip -- ip address of the device we try to reach
    port -- port of the device we try to reach
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((ip, port))

    """for _ in range(20): 
        sock.send(b"test\n")
        data = receive(sock)
        print(data.decode("utf-8"))
        time.sleep(1)"""

    while True:
        try:
            sock.send(b"test\n")
            data = receive(sock)
            print(data.decode("utf-8"))
            time.sleep(1)
        except socket.error:
            print("Connection closed.")
            break

def gen_msg(data):
    """
    Generate the message to send based on data processing

    Parameters
    ----------
    data -- data received, base processing on this data (str)

    Returns
    -------
    message -- the message to send (binary str)
    """
    
    # Append global counter at the end of the data
    data += str(global_counter)

    # Process data here then generate based on processing
    message = b"" + bin(data)[2:]

    global_counter += 1
    display_global_counter_message()

    return message

def display_global_counter_message():
    """
    Display the value of the global_counter variable.
    """
    print(f"The number of people seen is: {global_counter}.")


if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", dest="ip", type=str)
    parser.add_argument("--port", dest="port", type=int)
    args = parser.parse_args()

    main(args.ip, args.port)

