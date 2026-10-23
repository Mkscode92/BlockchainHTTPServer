output "url" {
  description = "Base URL of the deployed server"
  value       = "http://${aws_eip.server.public_ip}:${var.port}"
}

output "public_ip" {
  value = aws_eip.server.public_ip
}

output "ssh" {
  description = "SSH command (needs ssh_key_name set)"
  value       = "ssh -i <your-key.pem> ubuntu@${aws_eip.server.public_ip}"
}
