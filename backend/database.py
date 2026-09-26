import firebase_admin
from firebase_admin import credentials
from firebase_admin import db
import json
from fastapi.responses import JSONResponse
import json, datetime
from typing import Dict, List, Optional

# First app
cred1 = credentials.Certificate('Database.json')
default_app = firebase_admin.initialize_app(cred1, {
    'databaseURL': "https://spheric-shield-338406-default-rtdb.firebaseio.com"
})


# Main Ref
ref = db.reference('DCSD/', app=default_app)
attendent = ref.child('Attendence/')


def addAttendence(id: str, date: str, status: bool):
    date_ref = attendent.child(date)
    date_ref.update({id: status})

def getAttendence(date: str):
    rdata = {}
    date_ref = attendent.child(date)
    data = date_ref.get()

    def normalize_lectures(value):
        # Always return 4 lecture slots: [lec1, lec2, lec3, lec4].
        if isinstance(value, dict):
            return [1 if value.get(str(i), 0) else 0 for i in range(1, 5)]

        if isinstance(value, list):
            # Firebase lists often use index 1..4 with index 0 as None.
            return [1 if (value[i] if i < len(value) else 0) else 0 for i in range(1, 5)]

        present = 1 if value else 0
        return [present, present, present, present]

    if data is None:
        # Fallback if no records exist for the given date
        for i in range(1, 11):
            student_id = str(i).zfill(3)
            rdata[student_id] = [0, 0, 0, 0]
    else:
        # Dynamically iterate over all existing student IDs stored in Firebase
        for student_id, lectures in data.items():
            rdata[student_id] = normalize_lectures(lectures)

    return rdata

def getApprovalStatus(date: str):
    date_ref = attendent.child(date)
    data = date_ref.get()
    if data is None:
        return False
    else:
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
    
    print(date_ref.get())

