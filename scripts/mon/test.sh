#!/bin/bash
SERVER_URL=${1:-"http://localhost:8080/api/jobs"}

echo -e "\ntest_job1 should change"
echo -e "test_job2 should not change\n"
curl -X GET $SERVER_URL
echo -e "\n\nAdding test_job1:"
curl -X POST $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","tasks":[1,2,3]}'
echo -e "\n\nAdding test_job2:"
curl -X POST $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job2","tasks":[1,2,3]}'
echo -e "\n\nStarting task 1 in test_job1:"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":1,"action":"R","rc":0}'
echo -e "\n\ntask 1 in test_job1 finnished with code 0:"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":1,"action":"D","rc":0}'
echo -e "\n\nStarting task 2 in test_job1:"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":2,"action":"R","rc":0}'
echo -e "\n\nTesting finishing task 2 with code that is not number in test_job1 without stating. Should fail:"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":2,"action":"D","rc":"string"}'
echo -e "\n\ntask 2 in test_job1 finnished with code 1:"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":2,"action":"D","rc":1}'
echo -e "\n\nSkipping task 3 in test_job1:"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job1","task":3,"action":"S","rc":0}'
echo -e "\n\nTesting finishing task 1 with code -1 in test_job2 without stating. Should fail:"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job2","task":1,"action":"D","rc":-1}'
echo -e "\n\nTesting finishing task 2 with code 0 in test_job2 without stating. Should fail:"
curl -X PATCH $SERVER_URL  -H "Content-Type: application/json"   -d '{"name":"test_job2","task":2,"action":"D","rc":0}'
echo -e "\n"
curl -X GET $SERVER_URL
echo -e "\n"