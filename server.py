import socket
import argparse
import time
import json

# Global dictionary
# Each key is a node and the value is its counter of people
global_counter_save = {}

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

def data_treatment(data):
    """
    Function to treat the data accordingly to the format chosen

    Parameters
    ----------
    data: string with the data received
    """
    # Make sure it is pertinent data
    if not ',' in data:
        return

    # It should split the data in 2 parts (id, counter)
    data_split = data.split(",")
    
    node_id = data_split[0]
    node_counter = data_split[1]

    # Update value of node counter in the global save
    # Create value for dictionary if not already in keys
    if f"Node_{node_id}" not in global_counter_save.keys():
        global_counter_save[f"Node_{node_id}"] = node_counter
    # If already exists add to the counter
    else:
        global_counter_save[f"Node_{node_id}"] += node_counter

    # Display node id
    display_node_counter(node_id)

def display_global_counter_message():
    """
    Display the value of each node counter.

    Note
    ----
    Mostly used as a DEBUG function
    """
    for node_id, node_counter in global_counter_save.items():
        print(f"Node {node_id} -- Counter: {node_counter}")

def display_node_counter(node_id):
    """
    Display the value of the counter of a specific node

    Parameter
    ---------
    node_id -- id of the node of which we want to display the counter (str)
    """
    print(f"Node {node_id} -- Counter : {global_counter_save[f'Node_{node_id}']}")

def save_data(data_dict, file_name):
    """
    Saves data_dict into a file with file_name

    Parameters
    ----------
    data_dict -- dict with all data (dict)
    file_name -- name of the file (str)
    """
    with open(file_name, 'w') as save_file:
        json.dump(data_dict, save_file)

def main(ip, port, saveFile=False):
    """
    Main loop; communication establishment
    + exchange/receive messages with ip:port

    Parameters
    ----------
    ip -- ip address of the device we try to reach
    port -- port of the device we try to reach
    saveFile -- true if there is a file with an existing save of node counters
    """
    save_name = "global_counter_save.json"

    # Restore save from existing file
    if saveFile:
        with open(save_name, 'r') as file:
            global_counter_save = json.load(file)
            display_global_counter_message()


    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((ip, port))

    # As long as connection is running, keep the server up
    while True:
        try:
            data = receive(sock)
            print(data.decode("utf-8"))

            #data_treatment(data.decode("utf-8"))
                
            time.sleep(1)
        except socket.error:
            print("Connection closed.")
            break

    # Save dictionnary into a file when connection fails
    if saveFile:
        save_data(global_counter_save, save_name)

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", dest="ip", type=str)
    parser.add_argument("--port", dest="port", type=int)
    parser.add_argument("--save", dest="save", type=bool, default=False)
    args = parser.parse_args()

    #main(args.ip, args.port)
    main(args.ip, args.port, args.save)
