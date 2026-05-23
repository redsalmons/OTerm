#ifndef FILETRANSFERTASK_H
#define FILETRANSFERTASK_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct FileTransferTask {
    std::string id;
    std::string device_id;
    std::string create_date;
    std::string local;
    std::string remote;
    std::string action;
    long long size;
    std::string status;  // pending, processing, completed, failed
    int progress;  // 0-100
    std::string result;  // success or error message

    json to_json() const {
        return json{
            {"id", id},
            {"device_id", device_id},
            {"create_date", create_date},
            {"local", local},
            {"remote", remote},
            {"action", action},
            {"size", size},
            {"status", status},
            {"progress", progress},
            {"result", result}
        };
    }

    static FileTransferTask fromJson(const json& j) {
        FileTransferTask task;
        task.id = j.value("id", "");
        task.device_id = j.value("device_id", "");
        task.create_date = j.value("create_date", "");
        task.local = j.value("local", "");
        task.remote = j.value("remote", "");
        task.action = j.value("action", "");
        task.size = j.value("size", 0LL);
        task.status = j.value("status", "pending");
        task.progress = j.value("progress", 0);
        task.result = j.value("result", "");
        return task;
    }
};

struct FileTransferTaskList {
    std::string device_id;
    std::vector<FileTransferTask> tasks;

    json to_json() const {
        json j;
        j["device_id"] = device_id;
        j["tasks"] = json::array();
        for (const auto& task : tasks) {
            j["tasks"].push_back(task.to_json());
        }
        return j;
    }

    static FileTransferTaskList fromJson(const json& j) {
        FileTransferTaskList list;
        list.device_id = j.value("device_id", "");
        if (j.contains("tasks") && j["tasks"].is_array()) {
            for (const auto& task_json : j["tasks"]) {
                list.tasks.push_back(FileTransferTask::fromJson(task_json));
            }
        }
        return list;
    }
};

#endif
