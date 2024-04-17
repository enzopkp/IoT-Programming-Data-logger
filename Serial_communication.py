import tkinter as tk
from tkinter import ttk
import serial
import serial.tools.list_ports
import psycopg2

class SerialCommunicator(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Serial Communicator")
        self.geometry("800x600")

        self.ports = serial.tools.list_ports.comports()
        self.port_list = [str(port) for port in self.ports]

        self.create_widgets()

        # Connect to SQLite database
        self.conn = psycopg2.connect(
            user="postgres.vbvtzeaasbmxhuawttby",
            password="5E4LaLVjOFG1QCih",
            host="aws-0-ap-southeast-2.pooler.supabase.com",
            port="5432",
            database="postgres"
        )
        self.c = self.conn.cursor()
        self.buffer = ""

    def create_widgets(self):
        # Port selection dropdown
        port_label = tk.Label(self, text="Select Port:")
        port_label.pack(pady=5)
        self.port_var = tk.StringVar()
        port_dropdown = ttk.Combobox(self, textvariable=self.port_var, values=self.port_list)
        port_dropdown.pack(pady=5)

        # Data display area
        data_label = tk.Label(self, text="Received Data:")
        data_label.pack(pady=5)
        self.data_text = tk.Text(self, height=10, wrap=tk.WORD)
        self.data_text.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # Command input
        command_label = tk.Label(self, text="Send Command:")
        command_label.pack(pady=5)
        self.command_entry = tk.Entry(self, width=30)
        self.command_entry.pack(pady=5)
        send_button = tk.Button(self, text="Send", command=self.send_command)
        send_button.pack(pady=5)

        # Serial instance
        self.serial_inst = None

        # Configure the window to resize the Text widget
        self.bind("<Configure>", self.resize_text)

    def resize_text(self, event):
        text_width = event.width - 20
        text_height = event.height - 150
        self.data_text.config(width=text_width, height=text_height)

    def send_command(self, command=None):
        if command is None:
            command = self.command_entry.get()
        if self.serial_inst and self.serial_inst.is_open:
            self.serial_inst.write(command.encode() + b'\n')
            if command:  # Clear the entry only if a command was entered
                self.command_entry.delete(0, tk.END)
        else:
            print("Serial port is not open.")

    def update_data(self):
        if self.serial_inst and self.serial_inst.is_open:
            if self.serial_inst.in_waiting:
                packet = self.serial_inst.readline()
                data = packet.decode('utf').rstrip('\n')
                self.data_text.insert(tk.END, data + '\n')
                self.data_text.see(tk.END)
                self.process_data(data)
        self.after(100, self.update_data)

    def process_data(self, data):
        if data.startswith("ADDTO:cards"):
            parts = data.split(",")
            card_id = int(parts[1].split(":")[1])
            pressure = parts[2].split(":")[1].upper() == "TRUE"
            temperature = parts[3].split(":")[1].upper() == "TRUE"
            humidity = parts[4].split(":")[1].strip("\r\n").upper() == "TRUE"

            # Check if the card_id exists in the cards table
            self.c.execute("SELECT id FROM cards WHERE id = %s", (card_id,))
            existing_card = self.c.fetchone()

            if existing_card:
                # Update the existing record
                query = "UPDATE cards SET pressure = %s, temperature = %s, humidity = %s WHERE id = %s"
                values = (pressure, temperature, humidity, card_id)
                self.c.execute(query, values)
            else:
                # Insert a new record
                query = "INSERT INTO cards (id, pressure, temperature, humidity) VALUES (%s, %s, %s, %s)"
                values = (card_id, pressure, temperature, humidity)
                self.c.execute(query, values)

            self.conn.commit()
        elif data.startswith("ADDTO:data"):
            parts = data.split(",")
            card_id = int(parts[1].split(":")[1])

            # Check if the card_id exists in the cards table
            self.c.execute("SELECT id FROM cards WHERE id = %s", (card_id,))
            card = self.c.fetchone()

            if card:
                pressure = int(parts[2].split(":")[1])
                temperature = int(parts[3].split(":")[1])
                humidity = int(parts[4].split(":")[1])

                # Insert into data table
                query = "INSERT INTO data (card_id, pressure, temperature, humidity) VALUES (%s, %s, %s, %s)"
                values = (card_id, pressure, temperature, humidity)
                self.c.execute(query, values)

                self.conn.commit()
            else:
                print(f"Error: Card with ID {card_id} does not exist in the cards table.")
        elif data.startswith("GET:"):
            card_uid = int(data.split(":")[1])

            # Check if the card_uid exists in the cards table
            self.c.execute("SELECT pressure, temperature, humidity FROM cards WHERE id = %s", (card_uid,))
            card_data = self.c.fetchone()

            if card_data:
                pressure, temperature, humidity = card_data
                response = f"pressure:{pressure},temperature:{temperature},humidity:{humidity}"
                print(response)
                self.send_command(response)
            else:
                print(f"Error: Card with UID {card_uid} does not exist in the cards table.")
                self.send_command("Error: no match for card {card_uid} in the database.")
        elif data.startswith("DELETE:"):
            card_uid = int(data.split(":")[1])

            # Delete entries from the data table
            self.c.execute("DELETE FROM data WHERE card_id = (SELECT id FROM cards WHERE id = %s)", (card_uid,))

            # Set columns in the cards table to NULL
            self.c.execute("UPDATE cards SET pressure = NULL, temperature = NULL, humidity = NULL WHERE id = %s", (card_uid,))

            self.conn.commit()
            print(f"Deleted data and reset card settings for card with UID {card_uid}")

    def start_serial(self, event=None):
        port_var = self.port_var.get()
        if port_var:
            self.serial_inst = serial.Serial()
            self.serial_inst.baudrate = 9600
            self.serial_inst.port = port_var.split(' ')[0]  # Extract the port name from the string
            try:
                self.serial_inst.open()
                self.update_data()
            except serial.SerialException as e:
                print(f"Error opening serial port: {e}")

    def __del__(self):
        self.conn.close()

if __name__ == "__main__":
    app = SerialCommunicator()
    app.port_var.trace_add("write", lambda *args: app.start_serial())
    app.mainloop()