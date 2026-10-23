variable "region" {
  description = "AWS region to deploy into"
  type        = string
  default     = "us-east-1"
}

variable "instance_type" {
  description = "EC2 size; t3.micro is free-tier eligible"
  type        = string
  default     = "t3.micro"
}

variable "port" {
  description = "Port the server listens on (must match PORT in server.c)"
  type        = number
  default     = 8080
}

variable "api_allowed_cidrs" {
  description = "Who can reach the API. Keep this to your IP while /tamper is enabled."
  type        = list(string)
}

variable "ssh_allowed_cidr" {
  description = "Your IP for SSH, e.g. 203.0.113.7/32"
  type        = string
}

variable "ssh_key_name" {
  description = "Name of an existing EC2 key pair in the region (null = no SSH key)"
  type        = string
  default     = null
}

variable "repo_url" {
  description = "Public git URL the VM clones and builds"
  type        = string
  default     = "https://github.com/Mkscode92/BlockchainHTTPServer.git"
}

variable "branch" {
  description = "Branch to build"
  type        = string
  default     = "main"
}
