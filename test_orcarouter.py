import os

from openai import OpenAI

client = OpenAI(
    base_url="https://api.orcarouter.ai/v1",
    api_key=os.environ["ORCAROUTER_API_KEY"],
)

response = client.chat.completions.create(
    model="qwen/qwen3.8-27b-free",
    messages=[{"role": "user", "content": "Hello"}],
)
print(response.choices[0].message.content)
