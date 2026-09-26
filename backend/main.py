from typing import Dict

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from database import getAttendence, approveAttendance, getApprovalStatus, addLecAttend



app = FastAPI(title="Attendance API")

app.add_middleware(
	CORSMiddleware,
	allow_origins=["*"],
	allow_credentials=True,
	allow_methods=["*"],
	allow_headers=["*"],
)


class AttendanceUpdateRequest(BaseModel):
	id: str
	date: str
	lecture: int


class ApprovalUpdateRequest(BaseModel):
	approved_by_admin: bool


@app.get("/")
def root():
	return {"status": "success", "message": "Attendance API is running."}


@app.get("/getAttend")
def get_attendance_data(date: str):
	data = getAttendence(date)
	approval_status = getApprovalStatus(date)

	return {
		"status": "success",
		"date": date,
		"data": data,
		"approved_by_admin": approval_status,
	}


@app.post("/approveAttend")
def set_attendance_data(date: str,):
	approveAttendance(date, True)
	return {
		"status": "success",
		"message": "Attendance updated successfully.",
		"date": date,
		"value": True,
	}


@app.get("/getApprovalStatus")
def get_approval_status(date: str):
    approval_status = getApprovalStatus(date)
    return {
        "status": "success",
        "date": date,
        "approved_by_admin": approval_status,
    }

@app.post("/updateAttend")
def update_attendance(request: AttendanceUpdateRequest):
	addLecAttend(request.id, request.date, request.lecture)
	return {
		"status": "success",
		"message": "Attendance updated successfully.",
		"id": request.id,
		"date": request.date,
		"lecture": request.lecture,
	}