#include <aslam/common/sensor.h>

namespace aslam {

Sensor::Sensor(const SensorId& id) : id_(id) {
  CHECK(id_.isValid());
};

Sensor::Sensor(const SensorId& id, const std::string& topic)
    : id_(id), topic_(topic) {
  CHECK(id_.isValid());
};

void Sensor::setId(const SensorId& id) {
  id_ = id;
  CHECK(id_.isValid());
};

bool Sensor::isValid() const {
  if (!id_.isValid()) {
    LOG(ERROR) << "Invalid sensor id.";
    return false;
  }
  return isValidImpl();
}

void Sensor::setRandom() {
  generateId(&id_);
  setRandomImpl();
}

bool Sensor::deserialize(const YAML::Node& sensor_node) {
  CHECK(!sensor_node.IsNull());
  std::string id_as_string;
  if (YAML::safeGet(
          sensor_node, static_cast<std::string>(kYamlFieldNameId),
          &id_as_string)) {
    CHECK(!id_as_string.empty());
    CHECK(id_.fromHexString(id_as_string));
  } else {
    LOG(WARNING) << "Unable to find an ID field. Generating a new random id.";
    generateId(&id_);
  }
  CHECK(id_.isValid());

  if (getSensorType() != aslam::SensorType::kNCamera &&
      !YAML::safeGet(
          sensor_node, static_cast<std::string>(kYamlFieldNameTopic),
          &topic_)) {
    LOG(ERROR) << "Unable to retrieve the sensor topic.";
    return false;
  }

  return loadFromYamlNodeImpl(sensor_node);
}

void Sensor::serialize(YAML::Node* sensor_node_ptr) const {
  YAML::Node& sensor_node = *CHECK_NOTNULL(sensor_node_ptr);

  CHECK(id_.isValid());
  sensor_node[static_cast<std::string>(kYamlFieldNameId)] = id_.hexString();
  sensor_node[static_cast<std::string>(kYamlFieldNameSensorType)] =
      getSensorTypeString();
  if (getSensorType() != aslam::SensorType::kNCamera) {
    sensor_node[static_cast<std::string>(kYamlFieldNameTopic)] = topic_;
  }

  saveToYamlNodeImpl(&sensor_node);
}

}  // namespace aslam
