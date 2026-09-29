import firebase_admin
from firebase_admin import credentials, db
import datetime
from typing import Dict, List, Optional

# Initialize Firebase Admin SDK
cred1 = credentials.Certificate('Database.json')
default_app = firebase_admin.initialize_app(cred1, {
    'databaseURL': "https://spheric-shield-338406-default-rtdb.firebaseio.com"
})

# Database References
ref = db.reference('DCSD/', app=default_app)
attendent = ref.child('Attendence/')
main_gate_ref = ref.child('MainGate/')

def addAttendence(id: str, date: str, status: bool):
    date_ref = attendent.child(date)
    date_ref.update({id: status})

def getAttendence(date: str):
    rdata = {}
    date_ref = attendent.child(date)
    data = date_ref.get()

    def normalize_lectures(value):
        if isinstance(value, dict):
            return [1 if value.get(str(i), 0) else 0 for i in range(1, 5)]
        if isinstance(value, list):
            return [1 if (value[i] if i < len(value) else 0) else 0 for i in range(1, 5)]
        present = 1 if value else 0
        return [present, present, present, present]

    if data is None:
        for i in range(1, 11):
            student_id = str(i).zfill(3)
            rdata[student_id] = [0, 0, 0, 0]
    else:
        for student_id, lectures in data.items():
            if student_id != "approved_by_admin":
                rdata[student_id] = normalize_lectures(lectures)

    return rdata

def getApprovalStatus(date: str):
    date_ref = attendent.child(date)
    data = date_ref.get()
    if data is None:
        return False
    return data.get("approved_by_admin", False)

def approveAttendance(date: str, approved_by_admin: bool):
    date_ref = attendent.child(date)
    date_ref.update({"approved_by_admin": approved_by_admin})

def addLecAttend(id: int, date: str, lec: int):
    date_ref = attendent.child(date)
    student_id = str(id).zfill(3)
    date_ref.update({
        f"{student_id}/{lec}": 1
    })

# ============================================================================
# MAIN GATE SUBSYSTEM DATABASE FUNCTIONS
# ============================================================================
def recordMainGateEntry(id: str, date: str, timestamp: str = None):
    student_id = str(id).zfill(3)
    if not timestamp:
        timestamp = datetime.datetime.now().strftime("%H:%M:%S")
    
    gate_date_ref = main_gate_ref.child(date)
    gate_date_ref.child(student_id).push({
        "timestamp": timestamp,
        "status": "ENTERED"
    })

def getMainGateEntries(date: str):
    gate_date_ref = main_gate_ref.child(date)
    data = gate_date_ref.get()
    if data is None:
        return {}
    return data