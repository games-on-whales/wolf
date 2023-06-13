use std::{ffi::CString, os::unix::fs::MetadataExt, str::FromStr};

use smithay::{
    backend::{
        drm::{CreateDrmNodeError, DrmNode, NodeType},
        udev,
    },
    reexports::nix::sys::stat::major,
};

#[derive(Debug, Clone, PartialEq)]
pub enum RenderTarget {
    Hardware(DrmNode),
    Software,
}

impl FromStr for RenderTarget {
    type Err = CreateDrmNodeError;
    fn from_str(s: &str) -> Result<Self, CreateDrmNodeError> {
        Ok(match s {
            "software" => RenderTarget::Software,
            path => RenderTarget::Hardware(DrmNode::from_path(path)?),
        })
    }
}

impl Into<Option<DrmNode>> for RenderTarget {
    fn into(self) -> Option<DrmNode> {
        match self {
            RenderTarget::Hardware(node) => Some(node),
            RenderTarget::Software => None,
        }
    }
}

impl Into<RenderTarget> for DrmNode {
    fn into(self) -> RenderTarget {
        RenderTarget::Hardware(self)
    }
}

#[cfg(target_os = "linux")]
const NVIDIA_MAJOR: u64 = 195;

// no clue how this number is on BSDs, feel free to contribute

impl RenderTarget {
    pub fn as_devices(&self) -> Vec<CString> {
        match self {
            RenderTarget::Hardware(node) => {
                let mut devices = Vec::new();
                if let Some(primary) = node.dev_path_with_type(NodeType::Primary) {
                    devices.push(primary);
                }
                if let Some(render) = node.dev_path_with_type(NodeType::Render) {
                    devices.push(render);
                }
                if udev::driver(node.dev_id())
                    .ok()
                    .flatten()
                    .map(|s| s.to_str() == Some("nvidia"))
                    .unwrap_or(false)
                {
                    // no idea how match nvidia device nodes to kms/dri-nodes, so lets map all nvidia-nodes to be sure
                    for entry in std::fs::read_dir("/dev").expect("Unable to access /dev") {
                        if let Ok(entry) = entry {
                            if let Ok(metadata) = entry.metadata() {
                                if metadata.is_file() && major(metadata.dev()) == NVIDIA_MAJOR {
                                    devices.push(entry.path());
                                }
                            }
                        }
                    }
                }

                devices
                    .into_iter()
                    .flat_map(|path| {
                        path.to_str()
                            .map(String::from)
                            .and_then(|string| CString::new(string).ok())
                    })
                    .collect()
            }
            RenderTarget::Software => Vec::new(),
        }
    }
}
