/**
 * @file cp_employee.h
 * @brief Header for employee related stuff.
 */

/*
Copyright (C) 2002-2010 UFO: Alien Invasion.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef CLIENT_CL_EMPLOYEE
#define CLIENT_CL_EMPLOYEE

#define MAX_EMPLOYEES 512

/******* GUI STUFF ********/

void E_InitStartup(void);

/******* BACKEND STUFF ********/

/** @todo MAX_EMPLOYEES_IN_BUILDING should be redefined by a config variable that is
 * lab/workshop/quarters-specific, e.g.:
 * @code
 * if (!maxEmployeesInQuarter) maxEmployeesInQuarter = MAX_EMPLOYEES_IN_BUILDING;
 * if (!maxEmployeesWorkersInLab) maxEmployeesWorkersInLab = MAX_EMPLOYEES_IN_BUILDING;
 * if (!maxEmployeesInWorkshop) maxEmployeesInWorkshop = MAX_EMPLOYEES_IN_BUILDING;
 * @endcode
 */

/**
 * @brief The types of employees.
 */
typedef enum {
	EMPL_SOLDIER,
	EMPL_SCIENTIST,
	EMPL_WORKER,
	EMPL_PILOT,
	EMPL_ROBOT,
	MAX_EMPL		/**< For counting over all available enums. */
} employeeType_t;

/** The definition of an employee */
typedef struct employee_s {
	int idx;					/**< self link in global employee-list - this should be references only with the variable name emplIdx
								 * to let us find references all over the code easier @sa E_DeleteEmployee */

	base_t *baseHired;			/**< Base where the soldier is hired it atm. */

	char speed;					/**< Speed of this Worker/Scientist at research/construction. */

	building_t *building;		/**< Assigned to this building in ccs.buildings[baseIDXHired][buildingID] */

	qboolean transfer;			/**< Is this employee currently transferred?
								 * @sa Set in TR_TransferStart_f
								 * @todo make use of this wherever needed. */
	character_t chr;			/**< Soldier stats (scis/workers/etc... as well ... e.g. if the base is attacked) */
	employeeType_t type;		/**< back link to employee type in ccs.employees */
	struct nation_s *nation;	/**< What nation this employee came from. This is NULL if the nation is unknown for some (code-related) reason. */
	struct ugv_s *ugv;			/**< if this is an employee of type EMPL_ROBOT then this is a pointer to the matching ugv_t struct. For normal employees this is NULL. */
} employee_t;

void E_ResetEmployees(void);
employee_t* E_GetNext(employeeType_t type, employee_t *lastEmployee);
employee_t* E_GetNextFromBase(employeeType_t type, employee_t *lastEmployee, const base_t *base);
employee_t* E_GetNextHired(employeeType_t type, employee_t *lastEmployee);
employee_t* E_CreateEmployee(employeeType_t type, struct nation_s *nation, struct ugv_s *ugvType);
qboolean E_DeleteEmployee(employee_t *employee, employeeType_t type);
qboolean E_HireEmployee(base_t* base, employee_t* employee);
qboolean E_HireEmployeeByType(base_t* base, employeeType_t type);
qboolean E_HireRobot(base_t* base, const struct ugv_s *ugvType);
qboolean E_UnhireEmployee(employee_t* employee);
void E_RefreshUnhiredEmployeeGlobalList(const employeeType_t type, const qboolean excludeUnhappyNations);
qboolean E_RemoveEmployeeFromBuildingOrAircraft(employee_t *employee);
void E_ResetEmployee(employee_t *employee);
int E_GenerateHiredEmployeesList(const base_t *base);
qboolean E_IsAwayFromBase(const employee_t *employee);

employeeType_t E_GetEmployeeType(const char* type);
extern const char* E_GetEmployeeString(employeeType_t type);

employee_t* E_GetEmployee(const base_t* const base, employeeType_t type, int num);
employee_t* E_GetUnhiredRobot(const struct ugv_s *ugvType);
int E_GetHiredEmployees(const base_t* const base, employeeType_t type, linkedList_t **hiredEmployees);
employee_t* E_GetHiredRobot(const base_t* const base, const struct ugv_s *ugvType);
employee_t* E_GetUnassignedEmployee(const base_t* const base, employeeType_t type);
employee_t* E_GetAssignedEmployee(const base_t* const base, employeeType_t type);
employee_t* E_GetHiredEmployeeByUcn(const base_t* const base, employeeType_t type, int ucn);
employee_t* E_GetEmployeeFromChrUCN(int ucn);
qboolean E_MoveIntoNewBase(employee_t *employee, base_t *newBase);

int E_CountHired(const base_t* const base, employeeType_t type);
int E_CountHiredRobotByType(const base_t* const base, const struct ugv_s *ugvType);
int E_CountAllHired(const base_t* const base);
int E_CountUnhired(employeeType_t type);
int E_CountUnhiredRobotsByType(const struct ugv_s *ugvType);
int E_CountUnassigned(const base_t* const base, employeeType_t type);
employee_t* E_GetEmployeeByMenuIndex(int num);
void E_UnhireAllEmployees(base_t* base, employeeType_t type);
void E_DeleteAllEmployees(base_t* base);
void E_DeleteEmployeesExceedingCapacity(base_t *base);
qboolean E_IsInBase(const employee_t* empl, const base_t* const base);
void E_HireForBuilding(base_t* base, building_t * building, int num);
void E_InitialEmployees(void);
void E_Init(void);

void E_RemoveInventoryFromStorage(employee_t *employee);

#define E_IsHired(employee)	((employee)->baseHired != NULL)

#endif /* CLIENT_CL_EMPLOYEE */
