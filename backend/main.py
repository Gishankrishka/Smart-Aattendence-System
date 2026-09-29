from datetime import date
from typing import Dict

from fastapi import FastAPI, HTTPException, status
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

from database import (
    addLecAttend,
    approveAttendance,
    getApprovalStatus,
    getAttendence,
)

app = FastAPI(title="Attendance API")

# Update this list with your allowed frontend origins in production
ALLOWED_ORIGINS = [
    "http://localhost:3000",
    "http://localhost:5173",
]

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Replace with ALLOWED_ORIGINS in production
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# Request Models
class AttendanceUpdateRequest(BaseModel):
    id: str
    date: str
    lecture: int


class ApprovalRequest(BaseModel):
    date: str
    approved: bool = True


@app.get("/", tags=["Health Check"])
def root():
    return {"status": "success", "message": "Attendance API is running."}


@app.get("/getAttend", tags=["Attendance"])
def get_attendance_data(date: str):
    data = getAttendence(date)
    approval_status = getApprovalStatus(date)

    return {
        "status": "success",
        "date": date,
        "data": data,
        "approved_by_admin": approval_status,
    }


@app.get("/getApprovalStatus", tags=["Attendance"])
def get_approval_status(date: str):
    approval_status = getApprovalStatus(date)
    return {
        "status": "success",
        "date": date,
        "approved_by_admin": approval_status,
    }


@app.post("/approveAttend", tags=["Attendance"])
def set_attendance_data(payload: ApprovalRequest):
    approveAttendance(payload.date, payload.approved)
    return {
        "status": "success",
        "message": "Attendance approval updated successfully.",
        "date": payload.date,
        "value": payload.approved,
    }


@app.post("/updateAttend", tags=["Attendance"])
def update_attendance(request: AttendanceUpdateRequest):
    addLecAttend(request.id, request.date, request.lecture)
    return {
        "status": "success",
        "message": "Attendance updated successfully.",
        "id": request.id,
        "date": request.date,
        "lecture": request.lecture,
    }