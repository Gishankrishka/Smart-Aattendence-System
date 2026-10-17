from datetime import datetime
from typing import Dict, Optional

from fastapi import FastAPI, HTTPException, Query, status
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

from database import (
    addLecAttend,
    approveAttendance,
    getApprovalStatus,
    getAttendence,
    getCardMappings,
    getMainGateEntries,
    getSystemStatus,
    recordMainGateEntry,
    registerCardMapping,
    updateSystemStatus,
)

app = FastAPI(title="University IoT Attendance & Gate API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Request Models
class AttendanceUpdateRequest(BaseModel):
    id: str
    date: str
    lecture: int

class MainGateEntryRequest(BaseModel):
    id: str
    date: str
    time: Optional[str] = None

class CardRegisterRequest(BaseModel):
    uid: str
    student_id: str

class ApprovalRequest(BaseModel):
    date: str
    approved: bool = True

class SystemStatusRequest(BaseModel):
    queue_cm: int
    presence: bool

@app.get("/", tags=["Health Check"])
def root():
    return {"status": "success", "message": "University IoT System API is running."}

@app.get("/getAttend", tags=["Lecture Attendance"])
def get_attendance_data(date: str):
    data = getAttendence(date)
    approval_status = getApprovalStatus(date)
    return {
        "status": "success",
        "date": date,
        "data": data,
        "approved_by_admin": approval_status,
    }

@app.get("/getApprovalStatus", tags=["Lecture Attendance"])
def get_approval_status(date: str):
    approval_status = getApprovalStatus(date)
    return {
        "status": "success",
        "date": date,
        "approved_by_admin": approval_status,
    }

@app.post("/approveAttend", tags=["Lecture Attendance"])
def set_attendance_data(payload: ApprovalRequest):
    approveAttendance(payload.date, payload.approved)
    return {
        "status": "success",
        "message": "Attendance approval updated successfully.",
        "date": payload.date,
        "value": payload.approved,
    }

@app.post("/updateAttend", tags=["Lecture Attendance"])
def update_attendance(request: AttendanceUpdateRequest):
    addLecAttend(request.id, request.date, request.lecture)
    return {
        "status": "success",
        "message": "Lecture attendance updated successfully.",
        "id": request.id,
        "date": request.date,
        "lecture": request.lecture,
    }

@app.post("/updateMainGate", tags=["Main Gate"])
def update_main_gate(request: MainGateEntryRequest):
    entry_time = request.time or datetime.now().strftime("%H:%M:%S")
    recordMainGateEntry(request.id, request.date, entry_time)
    return {
        "status": "success",
        "message": "Main Gate entry logged.",
        "id": request.id,
        "date": request.date,
        "time": entry_time
    }

@app.get("/getMainGate", tags=["Main Gate"])
def get_main_gate(date: str):
    data = getMainGateEntries(date)
    return {
        "status": "success",
        "date": date,
        "data": data
    }

# Card Mapping Endpoints
@app.post("/registerCard", tags=["Card Mappings"])
def register_card(request: CardRegisterRequest):
    registerCardMapping(request.uid, request.student_id)
    return {
        "status": "success",
        "message": "Card mapping saved to database.",
        "uid": request.uid,
        "student_id": request.student_id
    }

@app.get("/getCardMappings", tags=["Card Mappings"])
def get_card_mappings():
    mappings = getCardMappings()
    return {
        "status": "success",
        "data": mappings
    }

# Live Hardware Telemetry Endpoints
@app.post("/updateSystemStatus", tags=["System Status"])
def update_system_status_endpoint(request: SystemStatusRequest):
    updateSystemStatus(request.queue_cm, request.presence)
    return {
        "status": "success",
        "message": "System status updated successfully.",
        "queue_cm": request.queue_cm,
        "presence": request.presence
    }

@app.get("/getSystemStatus", tags=["System Status"])
def get_system_status_endpoint():
    status_data = getSystemStatus()
    return {
        "status": "success",
        "queue_cm": status_data.get("queue_cm", 400),
        "presence": status_data.get("presence", False)
    }