use std::collections::HashMap;

type IniMap = HashMap<String, HashMap<String, String>>;

pub struct ParsedIni {
    values: IniMap,
    ordered_lists: HashMap<String, Vec<String>>,
}

impl ParsedIni {
    pub fn parse(contents: &str) -> Self {
        let mut values = IniMap::new();
        let mut ordered_lists = HashMap::new();
        let mut current_section = String::new();

        for raw_line in contents.lines() {
            let line = strip_comment(raw_line).trim();
            if line.is_empty() {
                continue;
            }

            if let Some(section) = line
                .strip_prefix('[')
                .and_then(|line| line.strip_suffix(']'))
            {
                current_section = section.trim().to_owned();
                values.entry(current_section.clone()).or_default();
                continue;
            }

            if let Some((key, value)) = line.split_once('=') {
                values
                    .entry(current_section.clone())
                    .or_default()
                    .insert(key.trim().to_owned(), value.trim().to_owned());
                continue;
            }

            ordered_lists
                .entry(current_section.clone())
                .or_insert_with(Vec::new)
                .push(line.to_owned());
        }

        Self {
            values,
            ordered_lists,
        }
    }

    pub fn get_string(&self, section: &str, key: &str) -> Option<&str> {
        self.values.get(section)?.get(key).map(String::as_str)
    }

    pub fn get_i64(&self, section: &str, key: &str, default_value: i64) -> i64 {
        self.get_string(section, key)
            .and_then(parse_i64)
            .unwrap_or(default_value)
    }

    pub fn get_bool(&self, section: &str, key: &str) -> Option<bool> {
        self.get_string(section, key).and_then(parse_bool)
    }

    pub fn get_f32(&self, section: &str, key: &str) -> Option<f32> {
        self.get_string(section, key)
            .and_then(|value| value.trim().parse().ok())
    }

    pub fn ordered_list_len(&self, section: &str) -> usize {
        self.ordered_lists
            .get(section)
            .map(Vec::len)
            .unwrap_or_default()
    }

    pub fn ordered_list_item(&self, section: &str, index: usize) -> Option<&str> {
        self.ordered_lists
            .get(section)?
            .get(index)
            .map(String::as_str)
    }
}

fn strip_comment(line: &str) -> &str {
    line.split_once(';').map(|(line, _)| line).unwrap_or(line)
}

fn parse_bool(value: &str) -> Option<bool> {
    match value.trim().to_ascii_lowercase().as_str() {
        "true" | "1" => Some(true),
        "false" | "0" => Some(false),
        _ => None,
    }
}

fn parse_i64(value: &str) -> Option<i64> {
    let value = value.trim();
    value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
        .map(|hex| i64::from_str_radix(hex, 16).ok())
        .unwrap_or_else(|| value.parse().ok())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_key_values_with_whitespace_and_comments() {
        let ini = ParsedIni::parse(
            r#"
            ; ignored
            [General]
            Enabled = true
            Count = 0x28 ; inline comment
            Scale = 1.25
            "#,
        );

        assert_eq!(ini.get_bool("General", "Enabled"), Some(true));
        assert_eq!(ini.get_i64("General", "Count", -1), 40);
        assert_eq!(ini.get_f32("General", "Scale"), Some(1.25));
    }

    #[test]
    fn returns_defaults_for_missing_or_invalid_i64_values() {
        let ini = ParsedIni::parse(
            r#"
            [Hooks]
            ValidNegative = -1
            Invalid = nope
            "#,
        );

        assert_eq!(ini.get_i64("Hooks", "ValidNegative", 10), -1);
        assert_eq!(ini.get_i64("Hooks", "Invalid", 10), 10);
        assert_eq!(ini.get_i64("Missing", "Value", 10), 10);
    }

    #[test]
    fn preserves_ordered_list_items() {
        let ini = ParsedIni::parse(
            r#"
            [UObjectBase]
            ProcessEvent
            StaticClass
            ; ignored
            GetWorld
            "#,
        );

        assert_eq!(ini.ordered_list_len("UObjectBase"), 3);
        assert_eq!(
            ini.ordered_list_item("UObjectBase", 0),
            Some("ProcessEvent")
        );
        assert_eq!(ini.ordered_list_item("UObjectBase", 2), Some("GetWorld"));
        assert_eq!(ini.ordered_list_item("UObjectBase", 3), None);
    }
}
